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
            'num-zones': 200,
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

def create_a_rrs(g, fqdn, zone, auth=None):
    all = {
    "ttl": 2592000,
    "a": [
        "127.0.0.1",
        "127.0.0.2",
        "127.0.0.3",
        "127.0.0.4"
    ]}
    if auth == None:
        auth=('admin', g['pass'])

    url = g['master-url'] + '/rr/' + fqdn

    return requests.post(url, data=json.dumps(all), auth=auth)

def create_all_rrs(g, fqdn, zone, auth=None):
    all = {
    "ttl": 2592000,
    "a": [
        "127.0.0.1",
        "127.0.0.2"
    ],
    "txt": [
        "Dogs are cool",
        "My dogs are the best ones"
    ],
    "mx": [
        {
        "priority": 10,
        "host": "mail.nsblast.com"
        }
    ],
    "hinfo": {
        "cpu": "Asome",
        "os": "Linux"
    },
    "rp": {
        "mbox": "teste",
        "txt": "whatever"
    },
    "afsdb": [
        {
        "subtype": 1,
        "host": "asfdb.nsblast.com"
        }
    ],
    "srv": [
        {
        "priority": 1,
        "weight": 2,
        "port": 22,
        "target": zone
        }
    ],
    "dhcid": "YmE4ODM1ZWMtODM5Zi0xMWVlLTgzNWYtYmZhN2ZlYzJmYWQwCg==",
    "openpgpkey": "YzE1NWU2M2EtODM5Zi0xMWVlLTk0YWQtYzdkMjM3ZWJmNTg4Cg==",
    "rr": [
        {
        "type": 42,
        "rdata": "YjZkYTYzZDQtODM5MC0xMWVlLWE4NGEtZjcxZjAxZDY4NTdlCg=="
        },
        {
        "type": 142,
        "rdata": "ZGU2NDYwZWUtODM5MC0xMWVlLTkxZmQtNmI1Njc1ZGRkOTFkCg=="
        }
    ]
    }

    if auth == None:
        auth=('admin', g['pass'])

    url = g['master-url'] + '/rr/' + fqdn

    return requests.post(url, data=json.dumps(all), auth=auth)

def test_create_zones_for_nsblast(global_data):
    num_zones = global_data['num-zones']
    for i in range(num_zones):
        zone = ''
        if i == 0:
            zone = 'example.com'
        else:
            zone = 'example-{}.com'.format(i)

        create_zone(global_data, zone)
        create_a_rrs(global_data, 'www.{}'.format(zone), zone)

def test_create_all_rrs(global_data):
    zone = 'a-example.com'
    create_zone(global_data, zone)
    create_all_rrs(global_data, 'all.' + zone, zone)
    for i in range(20):
        create_a_rrs(global_data, 'test-{}x.{}'.format(i, zone), zone)
