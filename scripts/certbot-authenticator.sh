#!/bin/bash

echo ==================================
echo "Set ${CERTBOT_DOMAIN}"
echo ==================================

curl --netrc -X PATCH "https://api.nsblast.com/api/v1/rr/_acme-challenge.${CERTBOT_DOMAIN}?append=true" \
        -H 'accept: application/json' \
        -H 'Content-Type: application/json' \
        -d "{\"txt\": \"${CERTBOT_VALIDATION}\", \"ttl\": 60}"

