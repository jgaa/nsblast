#!/bin/bash

echo ==================================
echo "Clean ${CERTBOT_DOMAIN}"
echo ==================================

curl --netrc -X DELETE "https://api.nsblast.com/api/v1/rr/_acme-challenge.${CERTBOT_DOMAIN}" \
        -H 'Content-Type: application/json' \
        -H 'accept: application/json'
