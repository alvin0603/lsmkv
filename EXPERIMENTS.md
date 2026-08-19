# Experiments

I use `lsmkv_bench` to run repeatable workloads against the same engine exposed by the public API. The raw runs and scripts are in [`bench/`](bench/).

## Dividing a 64 MiB allocation budget

This experiment sets a 64 MiB accounted budget across the MemTable flush threshold, Bloom bit arrays, and block-cache capacity. Process RSS, container metadata and the kernel page cache are outside this accounting. The run uses 500,000 preloaded keys with 256-byte values, 50,000 warmup operations, and 500,000 measured operations. The workload is 95% reads, 4% writes and 1% deletes with a Zipfian distribution (`theta = 0.99`), `kSyncOff`, and three runs per configuration.

![Read p99 latency](figures/p99.svg)

![Throughput](figures/throughput.svg)

![Block cache hit rate](figures/cache.svg)

What matters here is the tradeoff created by the 64 MiB cap. The Bloom filter, MemTable and block cache share the same pool, so giving more memory to either of the first two leaves less room for cached data blocks.

A small Bloom filter produces enough false positives to send many absent keys into the SSTable read path. Giving it more bits removes most of that wasted I/O, but the benefit soon flattens while the block cache keeps getting smaller. A larger MemTable has a similar tension. It batches more keys into each flush, which means fewer SSTables and less compaction I/O, then its throughput benefit levels off as cache pressure grows. The p99 and throughput curves are both showing these two effects pulling against each other.

The cache-hit graph has one trap. A stronger Bloom filter stops many negative reads before they ask the cache, so the remaining lookup stream is harder. Its hit rate can fall because the cache is smaller and because those easy negative lookups disappeared. I read it alongside p99 and throughput instead of treating hit rate alone as the answer.

The raw runs are in [`bench/memory.csv`](bench/memory.csv). To rerun the experiment or redraw the figures:

```bash
python3 bench/memory.py
python3 bench/memory.py --plot-only
```
