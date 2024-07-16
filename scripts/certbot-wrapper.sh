#!/bin/bash

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 email -d domain [-d domain ...]"
    exit 1
fi


if [ -z ${CERTBOT_FILES+x} ]; then
    CERTBOT_FILES=~/.certbot-files
fi

script_path=$(realpath "$0")
script_dir=$(dirname "$script_path")
email="$1"
shift

echo "Preparing to reques/update certs."
echo "Files are saved at: ${CERTBOT_FILES}"

if certbot certonly --manual --preferred-challenges=dns \
    --manual-auth-hook "${script_dir}/certbot-authenticator.sh" \
    --manual-cleanup-hook "${script_dir}/certbot-cleanup.sh" \
    --config-dir ${CERTBOT_FILES} --work-dir ${CERTBOT_FILES} --logs-dir ${CERTBOT_FILES} \
    --agree-tos -m "${email}" -q \
    "$@" ;
then
    echo "Successfully done"
else
    echo "Failed. Look for errors in ${CERTBOT_FILES}/letsencrypt.log"
fi
