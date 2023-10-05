#!/bin/bash

export NSBLAST_ADMIN_PASSWORD=VerySecret

die() {
    echo "$*" 1>&2
    exit 1;
}

certdir=/tmp/nsblast-testcerts

docker-compose down --timeout 1

if [ -d "${certdir}" ]
then
    echo "Removing old cert dir: ${certdir}"
    rm -rf "${certdir}"
fi

mkdir ${certdir} || die
# Create certs got gRPC
docker run --rm --name nsblast-cert -it -v ${certdir}:/certs -u $(id -u ${USER}):$(id -g ${USER})  jgaafromnorth/nsblast --create-cert-subject master --create-certs-path /certs

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
