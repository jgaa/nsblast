# Functional tests

This test assumes that the nsblast docker container is built.

It start 3 instances of nsblast, where one act as a master server
and the other two as slaves.

The test requires Python 3 and pythons virtual environment.

```bash
bash setup.sh

bash run.sh
```

If you want to capture the logs from the session, you can enter this command
from the same directory, but in another shell:

```
docker-compose logs -f > /tmp/tests.log
```
