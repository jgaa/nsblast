# https://grpclib.readthedocs.io/_/downloads/en/latest/pdf/

# python -m grpc_tools.protoc -I ../../src/lib/proto --python_out=. --pyi_out=. --grpclib_python_out=. ../../src/lib/proto/nsblast-grpc.proto ../../src/lib/proto/nsblast.proto

# pip install grpcio-tools
# pip install grpclib

from __future__ import print_function

import asyncio
from grpclib.client import Channel
import threading
import logging
import random
import time
import pprint

import grpc

import nsblast_pb2
import nsblast_grpc_pb2
import nsblast_grpc_grpc

async def main():
    channel = Channel('localhost', 101234);
    stub = nsblast_grpc_grpc.NsblastSvcStub(channel)
    async with stub.Sync.open() as stream:
        await stream.send_request() # needed to initiate a call
        await stream.send_message(nsblast_grpc_pb2.SyncRequest(startAfter=0, level=1)) # Start
        while True:
            update = await stream.recv_message()
            await stream.send_message(nsblast_grpc_pb2.SyncRequest(startAfter=update.trx.id, level=1)) # Start

if __name__ == "__main__":
    asyncio.run(main())
