#!/bin/bash

export NSBLAST_ADMIN_PASSWORD=VerySecret

die() {
    echo "$*" 1>&2
    exit 1;
}


docker-compose down --timeout 1
docker-compose up -d || die

docker-compose logs -f --no-color > /tmp/nsblast-cluster-1m2s-logs.log &

echo "Waiting for back-ends to start"
sleep 3

. .venv/bin/activate
pytest tests.py
rval=$?

docker-compose kill -s SIGQUIT
docker-compose down --timeout 1

echo "Exiting with: ${rval}" 
exit $rval
