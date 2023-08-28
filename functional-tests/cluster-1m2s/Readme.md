# Functional tests - replication

This test assumes that the nsblast docker container is built.

It start 3 instances of nsblast, where one act as a primary server
and the other two as slaves, using the gRPC based RocksDB replication
between the primary and the followers.

The test requires docker-compose, Python 3 and pythons virtual environment.

```bash
bash setup.sh

bash run.sh
```

The logs from the containers are piped to `/tmp/nsblast-cluster-1m2s-logs.log`
