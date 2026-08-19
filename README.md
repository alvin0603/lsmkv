# lsmkv

I built `lsmkv` to understand a storage engine below its API. I wanted to follow a write from its log record, through an ordered in-memory table, into immutable files, then recover the correct version after a crash. That meant working directly with partial writes, CRCs, file descriptors, `fsync`/`rename` ordering, and the read/write costs of an LSM tree.

`lsmkv` is a single-process embedded key-value storage engine written in C++20. Its public API provides point reads, writes, deletes and full ordered iteration. The storage path includes WAL recovery, MemTable flush, immutable SSTables, a Manifest, synchronous L0-to-L1 compaction, per-table Bloom filters and a shared LRU block cache. It runs on Linux, holds an exclusive lock on the database directory, and performs flush and compaction in the caller's thread.

## How it works

![lsmkv architecture](figures/architecture.svg)

**WAL and MemTable.** A write lives in both places before it reaches an SSTable, for two different jobs. The WAL is an append-only recovery record. The MemTable holds the same operations in sorted order, which makes point reads cheap and gives the SSTable writer an already sorted input. On restart, valid WAL records rebuild the MemTable.

`put()` assigns a sequence number, appends the encoded operation to the WAL, applies the selected sync policy, and then inserts it into the active MemTable. When the size threshold is reached, the DB creates the next WAL before freezing the old MemTable. The frozen table remains readable until its SSTable has been written and committed through the Manifest.

With the default `kSyncEveryWrite` mode, `put()` returns success only after the WAL `fsync` completes. `kSyncEveryN` and `kSyncOff` reduce sync calls with a weaker crash guarantee.

**SSTables and reads.** An SSTable is written sequentially and never edited in place. New values and deletions stay alongside older versions in other files. A point lookup searches in this order:

```text
active MemTable
    -> immutable MemTable
    -> Level 0 SSTables, newest file_id first
    -> Level 1 SSTables, newest file_id first
```

Each `SSTableReader` loads its sparse index and Bloom filter when it opens. A definite Bloom miss skips the file. A possible match continues to an index binary search for one candidate data block. The shared cache uses `(file_id, block_offset)` as its key; only a cache miss calls `pread`. The selected block is scanned linearly.

Ordered iteration creates one iterator for every live MemTable and SSTable. `MergingIterator` uses a min-heap to discard older versions of the same user key. `DBIterator` then hides a key when its newest entry is a tombstone.

**Versions and compaction.** The sequence number is stored in the `InternalKey`, so the newest operation for one user key sorts first. A tombstone has to remain as a record because an older put may still live in another immutable file.

Every flush adds another overlapping L0 file and increases the number of places a read may have to check. When L0 reaches its trigger, `lsmkv` merges all L0 files into one new L1 SSTable. It keeps the newest entry among those L0 inputs and retains tombstones. Existing L1 files do not join this merge, so their key ranges may overlap. The new L1 file is committed in the Manifest before the old L0 files are removed.

## Using it

```cpp
#include <lsmkv/db.h>

#include <iostream>
#include <string>

int main()
{
    auto db = lsmkv::DB::open("data");
    if(!db || !db->put("cat", "orange"))
        return 1;

    std::string value;
    if(db->get("cat", value) == lsmkv::LookupResult::kFound)
        std::cout << value << '\n';

    for(auto it = db->newIterator(); it && it->valid(); it->next())
        std::cout << it->key() << " = " << it->value() << '\n';
}
```

After building the library, compile the example with:

```bash
g++ -std=c++20 -Iinclude demo.cpp build/liblsmkv.a -o demo
```

## Bytes on disk

![InternalKey, WAL, SSTable and Manifest layouts](figures/storage-format.svg)

Fixed32 and Fixed64 fields use little-endian encoding. Key and value lengths use Varint32. An `InternalKey` is encoded as:

```text
user_key || Fixed64((sequence << 8) | type)
```

User keys sort in ascending order, while versions of the same key sort by descending sequence number. For one key, the first matching entry is its newest operation.

The WAL header stores payload length and CRC32. Replay stops at a partial header, partial payload, invalid length or CRC mismatch, then truncates the file to `last_valid_offset`. SSTable data is split into roughly 4 KiB blocks. The index stores each block's last `InternalKey`, offset and size; the fixed 40-byte footer tells the reader where the index and Bloom block begin.

## Crash recovery

SSTable filenames alone cannot describe one atomic database state during flush or compaction. The Manifest records the live file set, next file ID, last sequence number and the WAL epoch already covered by committed SSTables.

```text
write and fsync the new SSTable
    -> write MANIFEST.tmp
    -> fsync MANIFEST.tmp
    -> rename MANIFEST.tmp to MANIFEST
    -> fsync the database directory
    -> remove replaced WAL or SSTable files
```

The new state is durable after the directory `fsync` succeeds. If execution stops earlier, recovery uses the complete Manifest that remains, removes SSTables absent from it, and replays WAL epochs newer than `durable_wal_epoch`. Old WAL and compaction inputs are kept until after the commit, so the data needed by the previous state is still present during that window.

`open()` also validates the size and footer/index structure of every referenced SSTable, truncates an incomplete WAL tail, and resumes the global sequence number from the newest Manifest or replayed WAL record.

## Build and inspect

Requirements: Linux, CMake 3.16 or newer, and a C++20 compiler.

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Catch2 is vendored and used only by the tests. The plotting script uses Python 3 without third-party packages.

```bash
./build/lsmkv_bench --help
./build/sstable_dump <file.sst>
```

## Source layout

```text
lsmkv/
├── include/lsmkv/       public API and storage-format headers
├── src/
│   ├── db.cpp           DB state, recovery, flush and compaction trigger
│   ├── wal_*.cpp        WAL append, sync, replay and tail truncation
│   ├── memtable.cpp     ordered in-memory versions
│   ├── sstable_*.cpp    immutable table writer and reader
│   ├── manifest.cpp     snapshot encoding and atomic replacement
│   ├── *_iterator.cpp   k-way merge and public ordered scan
│   ├── bloom_filter.cpp per-SSTable negative lookup filter
│   ├── block_cache.cpp  shared byte-capacity LRU
│   └── internal/        helpers kept out of the public API
├── tests/               unit, recovery and integration tests
├── bench/               workload, metrics, raw data and plot script
├── tools/               SSTable inspection CLI
├── figures/             README diagrams and benchmark plots
├── third_party/         vendored Catch2
└── CMakeLists.txt
```

## Measurements

### Dividing a 64 MiB allocation budget

This experiment sets a 64 MiB accounted budget across the MemTable flush threshold, Bloom bit arrays, and block-cache capacity. Process RSS, container metadata and the kernel page cache are outside this accounting. The run uses 500,000 preloaded keys with 256-byte values, 50,000 warmup operations, and 500,000 measured operations. The workload is 95% reads, 4% writes and 1% deletes with a Zipfian distribution (`theta = 0.99`), `kSyncOff`, and three runs per configuration. Faded points are individual runs; the line is their median.

![Read p99 latency](figures/p99.svg)

![Throughput](figures/throughput.svg)

![Block cache hit rate](figures/cache.svg)

At 2 Bloom bits per key, read p99 is 196.3 µs and throughput is 62.1k ops/s. At 6 bits, they improve to 188.8 µs and 72.9k ops/s. Results from 6 to 14 bits stay close, so the Bloom benefit is already near its plateau for this workload.

For the MemTable, throughput rises from 67.1k ops/s at 4 MiB to 77.0k at 16 MiB, then reaches 77.7k at 32 MiB. Block-cache hit rate falls from 81.5% to 65.8% over the same settings. The 32 MiB setting does not raise p99 in these runs, so the cache pressure is visible in hit rate without a latency reversal yet.

The Bloom-filter cache result is easy to misread. A more accurate filter prevents many cache lookups from happening: hits fall from about 388k to 167k, while misses stay near 56k. The lower hit rate mostly reflects a different lookup stream.

```bash
python3 bench/memory.py
python3 bench/memory.py --plot-only
```
