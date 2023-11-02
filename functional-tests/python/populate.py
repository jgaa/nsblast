import requests
import yaml
import json
import pytest
import dns.resolver
import time
import os
import uuid

@pytest.fixture(scope = 'module')
def global_data():
    mresolver = dns.resolver.Resolver(configure=False)
    mresolver.nameservers = ['127.0.0.1']
    mresolver.port = 5354

    return {'master-url': os.getenv('NSBLAST_URL', 'http://127.0.0.1:8080/api/v1'),
            'num-zones': 1000,
            'pass': os.environ['NSBLAST_ADMIN_PASSWORD']
           }

def create_zone(g, fqdn, auth=None):
    if auth == None:
        auth=('admin', g['pass'])

    url = g['master-url'] + '/zone/' + fqdn

    zone = {'soa':
            { 'refresh': 2000,
             'retry': 3000,
             'expire': 4000,
             'minimum': 5000,
             'mname': 'master',
             'rname': 'hostmaster.' + fqdn},
             'ns': ['master', 'ns2', 'ns3'],
             'a': ['127.0.0.1', '127.0.0.2'],
             'mx': [
                {'priority': 10,
                 'host': 'mail.' + fqdn}
             ]
            }

    return requests.post(url, data=json.dumps(zone), auth=auth)

def test_create_zones_for_nsblast(global_data):
    num_zones = global_data['num-zones']
    for i in range(num_zones):
        if i == 0:
            create_zone(global_data, 'example.com')
        else:
            create_zone(global_data, 'example-{}.com'.format(i))
