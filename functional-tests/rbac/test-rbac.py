import requests
import yaml
import json
import pytest
import dns.resolver
import time
import os

def create_zone(g, fqdn):
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
    
    return requests.post(url, data=json.dumps(zone), auth=('admin', g['pass']), params={'wait': g['wait']})

def create_tenant(g, name):
    allperms = ['USE_API', 'READ_ZONE', 'LIST_ZONES', 'CREATE_ZONE', 'UPDATE_ZONE', 
            'DELETE_ZONE', 'READ_VZONE', 'LIST_VZONES', 'CREATE_VZONE', 'UPDATE_VZONE',
            'DELETE_VZONE', 'READ_RR', 'LIST_RRS', 'CREATE_RR', 'UPDATE_RR', 'DELETE_RR', 
            'CREATE_APIKEY', 'LIST_APIKEYS', 'GET_APIKEY', 'DELETE_APIKEY', 'GET_SELF_USER', 
            'DELETE_SELF_USER', 'CREATE_USER', 'LIST_USERS', 'GET_USER', 'UPDATE_USER', 
            'DELETE_USER', 'CREATE_ROLE', 'LIST_ROLES', 'GET_ROLE', 'UPDATE_ROLE', 'DELETE_ROLE', 
            'GET_SELF_TENANT', 'UPDATE_SELF_TENANT', 'DELETE_SELF_TENANT']
    
    wperms = ['CREATE_RR', 'UPDATE_RR', 'DELETE_RR', 'CREATE_APIKEY', 'DELETE_SELF_USER']
    
    roperms =  ['USE_API', 'READ_ZONE', 'LIST_ZONES', 
            'READ_VZONE', 'LIST_VZONES', 
            'READ_RR', 'LIST_RRS', 'LIST_APIKEYS', 'GET_APIKEY', 'GET_SELF_USER', 
            'LIST_ROLES', 'GET_ROLE', 'GET_SELF_TENANT']

    tenant = {
        'active': True,
        'allowedPermissions': allperms,
        'roles': [
            {
                'name': 'super',
                'permissions': allperms,
            },
            {
                'name': 'write',
                'permissions': wperms,
            },
            {
                'name': 'read',
                'permissions': roperms,
            }
        ],
        'users': [
            {'name': 'admin@' + name,
             'active': True,
             'roles': ['super'],
             'auth': {'password': 'teste'}
             },
             {'name': 'normaluser@' + name,
             'active': True,
             'roles': ['write', "read"],
             'auth': {'password': 'teste'}
             },
             {'name': 'rouser@' + name,
             'active': True,
             'roles': ['read'],
             'auth': {'password': 'teste'}
             }
        ]
        }
    url = g['master-url'] + '/tenant'
    r = requests.post(url, data=json.dumps(tenant), auth=('admin', g['pass']), params={'wait': g['wait']})
    assert r.ok
    v = r.json();
    id = v['value']['id']
    print('Created user {}', id)
    return id

@pytest.fixture(scope = 'module')
def global_data():
    mresolver = dns.resolver.Resolver(configure=False)
    mresolver.nameservers = ['127.0.0.1']
    mresolver.port = 5354

    slave1 = dns.resolver.Resolver(configure=False)
    slave1.nameservers = ['127.0.0.1']
    slave1.port = 5355

    slave2 = dns.resolver.Resolver(configure=False)
    slave2.nameservers = ['127.0.0.1']
    slave2.port = 5356
    password = os.environ['NSBLAST_ADMIN_PASSWORD']

    return {'master-dns': mresolver,
            'slave1': slave1,
            'slave2': slave2,
            #'master-url': os.getenv('NSBLAST_URL', 'http://127.0.0.1:8080/api/v1'),
            'master-url': 'http://localhost:8080/api/v1',
            'slave1-url': 'http://127.0.0.1:8081/api/v1',
            'slave2-url': 'http://127.0.0.1:8082/api/v1',
            'pass': password,
            'num-zones': 1000,
            'wait': 0
           }

def test_bootstrap(global_data):
    #assert create_zone(global_data, "example.com").ok
    tid = create_tenant(global_data, "test1")

# Create tenant, roles, users
# Let user create zones
# Let read-only user read but fail to update/create zones
# Try to access other tenants zones