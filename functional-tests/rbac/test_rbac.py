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

    slave1 = dns.resolver.Resolver(configure=False)
    slave1.nameservers = ['127.0.0.1']
    slave1.port = 5355

    slave2 = dns.resolver.Resolver(configure=False)
    slave2.nameservers = ['127.0.0.1']
    slave2.port = 5356
    password = os.environ['NSBLAST_ADMIN_PASSWORD']

    num_tenants = 5
    tenants = {}
    for t in range(num_tenants):
        zones = {}
        tname = 'tenant-{}'.format(t)
        for z in range(t):
            zname = ''
            if z == 0:
                zname = '{}.com'.format(tname)
            else:
                zname = 'z{}-{}.com'.format(z, tname)
            zones[zname] = {}
        tenants[tname] = {
            'id': str(uuid.uuid4()),
            'name': tname,
            'zones': zones,
            'auth': {'super': ('admin@{}'.format(tname), 'teste'), 
                     'normal':('normaluser@{}'.format(tname), 'tester'), 
                     'guest': ('rouser@{}'.format(tname), 'xteste')
                     }
            }

    return {'master-dns': mresolver,
            'slave1': slave1,
            'slave2': slave2,
            'master-url': os.getenv('NSBLAST_URL', 'http://127.0.0.1:8080/api/v1'),
            #'master-url': 'http://localhost:8080/api/v1',
            'slave1-url': 'http://127.0.0.1:8081/api/v1',
            'slave2-url': 'http://127.0.0.1:8082/api/v1',
            'pass': password,
            'num-zones': 1000,
            'wait': 0,
            'tenants': tenants
           }


def create_zone(g, fqdn, auth):
    # if auth == None:
    #     auth=('admin', g['pass'])

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
    
    return requests.post(url, data=json.dumps(zone), auth=auth, params={'wait': g['wait']})

def create_tenant(g, tenant, auth=None):
    if auth == None:
        auth=('admin', g['pass'])

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
    
    print("creating tenant: {}".format(tenant))

    t = {
        'id': tenant['id'],
        'name': tenant['name'],
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
            {'name': tenant['auth']['super'][0],
             'active': True,
             'roles': ['super'],
             'auth': {'password': tenant['auth']['super'][1]}
             },
             {'name': tenant['auth']['normal'][0],
             'active': True,
             'roles': ['write', "read"],
             'auth': {'password': tenant['auth']['normal'][1]}
             },
             {'name': tenant['auth']['guest'][0],
             'active': True,
             'roles': ['read'],
             'auth': {'password': tenant['auth']['guest'][1]}
             }
        ]
        }
    url = g['master-url'] + '/tenant'
    return requests.post(url, json=t, auth=auth, params={'wait': g['wait']})

def create_default_tenant(g, tname, auth=None):
    tenant = {
        'id': str(uuid.uuid4()),
        'name': tname,
        'auth': {'super': ('admin@{}'.format(tname), 'teste'), 
                 'normal':('normaluser@{}'.format(tname), 'tester'), 
                 'guest': ('rouser@{}'.format(tname), 'xteste')
                }
        }

    assert create_tenant(g, tenant, auth=auth).ok
    return tenant

def list_something(g, what, auth):
    url = '{}/{}'.format(g['master-url'], what)
    return requests.get(url, auth=auth, params={'wait': g['wait']})

def get_something(g, what, item, auth):
    url = '{}/{}/{}'.format(g['master-url'], what, item)
    return requests.get(url, auth=auth, params={'wait': g['wait']})

def create_something(g, what, item, data, auth):
    url = '{}/{}/{}'.format(g['master-url'], what, item)
    return requests.post(url, json=data, auth=auth, params={'wait': g['wait']})

def put_something(g, what, item, data, auth):
    url = '{}/{}/{}'.format(g['master-url'], what, item)
    return requests.put(url, json=data, auth=auth, params={'wait': g['wait']})

def patch_something(g, what, item, data, auth):
    url = '{}/{}/{}'.format(g['master-url'], what, item)
    return requests.patch(url, json=data, auth=auth, params={'wait': g['wait']})

def delete_something(g, what, item, auth):
    url = '{}/{}/{}'.format(g['master-url'], what, item)
    return requests.delete(url, auth=auth, params={'wait': g['wait']})

def test_bootstrap(global_data):
    print('Creating example.com zone owned by nsblast')
    #assert create_zone(global_data, "example.com").ok

    print('Creating tenants as admin')
    for name, tenant in global_data['tenants'].items():
        assert create_tenant(global_data, tenant).ok

# Create zones as tenants/super
def test_create_zones(global_data):
    for tname, td in global_data['tenants'].items():
        auth=td['auth']['super']
        for zfqdn, zd in td['zones'].items():
            assert create_zone(global_data, zfqdn, auth).ok

def test_normal_user_can_crud_rr(global_data):
    for tname, td in global_data['tenants'].items():
        auth=td['auth']['normal']

        # List zones
        assert list_something(global_data, "zone", auth).ok

        for zfqdn, zd in td['zones'].items():
            # List RR's
            assert get_something(global_data, "zone", zfqdn, auth).ok

            # Get RR for zone
            assert get_something(global_data, "rr", zfqdn, auth).ok

            www = "www.{}".format(zfqdn)
            assert not get_something(global_data, "rr",www, auth).ok

            url = '{}/rr/{}'.format(global_data['master-url'], www)
            assert requests.post(url,
                                 json={'a':['127.0.0.1', '127.0.0.2', '127.0.0.3']}, 
                                 auth=auth, params={'wait': global_data['wait']}).ok
            assert requests.put(url,
                                 json={'a':['127.0.0.2', '127.0.0.3']}, 
                                 auth=auth, params={'wait': global_data['wait']}).ok
            assert requests.patch(url,
                                 json={'txt':['teste']}, 
                                 auth=auth, params={'wait': global_data['wait']}).ok
            assert requests.delete(url + '/txt',
                                 auth=auth, params={'wait': global_data['wait']}).ok
            assert requests.delete(url,
                                 auth=auth, params={'wait': global_data['wait']}).ok
            assert not requests.delete(url,
                                 auth=auth, params={'wait': global_data['wait']}).ok

def test_guests_can_read_and_list_but_not_change(global_data):
    for tname, td in global_data['tenants'].items():
        auth=td['auth']['guest']

        # List zones
        assert list_something(global_data, "zone", auth).ok

        for zfqdn, zd in td['zones'].items():
            # List RR's
            assert get_something(global_data, "zone", zfqdn, auth).ok

            # Get RR for zone
            assert get_something(global_data, "rr", zfqdn, auth).ok

            www = "www.{}".format(zfqdn)
            assert not get_something(global_data, "rr",www, auth).ok

            url = '{}/rr/{}'.format(global_data['master-url'], www)
            assert requests.post(url,
                                 json={'a':['127.0.0.1', '127.0.0.2', '127.0.0.3']}, 
                                 auth=auth, params={'wait': global_data['wait']}).status_code == 403
            assert requests.put(url,
                                 json={'a':['127.0.0.2', '127.0.0.3']}, 
                                 auth=auth, params={'wait': global_data['wait']}).status_code == 403
            assert requests.patch(url,
                                 json={'txt':['teste']}, 
                                 auth=auth, params={'wait': global_data['wait']}).status_code == 403
            assert requests.delete(url + '/txt',
                                 auth=auth, params={'wait': global_data['wait']}).status_code == 403
            assert requests.delete(url,
                                 auth=auth, params={'wait': global_data['wait']}).status_code == 403
            assert requests.delete(url,
                                 auth=auth, params={'wait': global_data['wait']}).status_code == 403

def test_users_can_not_access_other_tenants(global_data):
    it =  iter(global_data['tenants'].items())

     # Auth details from first tenant's super user
    ak, av = next(it)

    # data/zones from second  tenant
    tname, td = next(it)

    print('td is {}'.format(td))

    auth=av['auth']['super']

    for zfqdn, zd in td['zones'].items():
        url = '{}/zone/{}'.format(global_data['master-url'], zfqdn)
        assert requests.delete(url + '/a',
                              auth=auth, params={'wait': global_data['wait']}).status_code == 403
        assert requests.delete(url,
                              auth=auth, params={'wait': global_data['wait']}).status_code == 403
        
        url = '{}/rr/{}'.format(global_data['master-url'], zfqdn)
        assert requests.post(url,
                                 json={'a':['127.0.0.1', '127.0.0.2', '127.0.0.3']}, 
                                 auth=auth, params={'wait': global_data['wait']}).status_code == 403
        assert requests.put(url,
                                json={'a':['127.0.0.2', '127.0.0.3']}, 
                                auth=auth, params={'wait': global_data['wait']}).status_code == 403
        assert requests.patch(url,
                                json={'txt':['teste']}, 
                                auth=auth, params={'wait': global_data['wait']}).status_code == 403
        assert requests.delete(url + '/txt',
                                auth=auth, params={'wait': global_data['wait']}).status_code == 403
        assert requests.delete(url,
                                auth=auth, params={'wait': global_data['wait']}).status_code == 403
        
        url = '{}/rr/www.{}'.format(global_data['master-url'], zfqdn)
        assert requests.post(url,
                                 json={'a':['127.0.0.1', '127.0.0.2', '127.0.0.3']}, 
                                 auth=auth, params={'wait': global_data['wait']}).status_code == 403
        assert requests.put(url,
                                json={'a':['127.0.0.2', '127.0.0.3']}, 
                                auth=auth, params={'wait': global_data['wait']}).status_code == 403
        assert requests.patch(url,
                                json={'txt':['teste']}, 
                                auth=auth, params={'wait': global_data['wait']}).status_code == 403
        assert requests.delete(url + '/txt',
                                auth=auth, params={'wait': global_data['wait']}).status_code == 403
        assert requests.delete(url,
                                auth=auth, params={'wait': global_data['wait']}).status_code == 403
        
def test_admin_can_revoke_perms(global_data):
    it =  iter(global_data['tenants'].items())

    next(it) # skip first

    # Use second
    tname, td = next(it)
    auth=td['auth']['super']

    # Create a new role with api + list zones perms
    role = {'name': 'myown', 'permissions':["USE_API", "LIST_ZONES"]}
    url = '{}/role'.format(global_data['master-url'])
    assert requests.post(url, json=role, auth=auth, params={'wait': global_data['wait']}).status_code == 201

    # Create a new user using this role
    user={'id': str(uuid.uuid4()),
          'name': 'testperms',
          'roles': ['myown'],
          'auth': {'password' : 'verysecret'}
          }
    uauth = (user['name'], user['auth']['password'])
    url = '{}/user'.format(global_data['master-url'])
    assert requests.post(url, json=user, auth=auth, params={'wait': global_data['wait']}).status_code == 201

    # Validate that the user can list zones
    assert list_something(global_data, "zone", uauth).ok

    # Remove the list zone permission from the role
    role['permissions'] = ["USE_API"]
    url = '{}/role/{}'.format(global_data['master-url'], role['name'])
    assert requests.put(url, json=role, auth=auth, params={'wait': global_data['wait']}).ok

    # Validate that user cannot list zones
    assert list_something(global_data, "zone", uauth).status_code == 403

    # Delete the role
    url = '{}/role/myown'.format(global_data['master-url'])
    assert requests.delete(url, auth=auth, params={'wait': global_data['wait']}).ok

    # Delete user
    url = '{}/user/{}'.format(global_data['master-url'], user['name'])
    assert requests.delete(url, auth=auth, params={'wait': global_data['wait']}).ok

def get_second_tenant(g):
    it =  iter(g['tenants'].items())
    next(it) # skip first
    rval = True

    # Use second
    _, td = next(it)
    return td

def create_user_with_permission(g, perm, auth, username='testperms', rolename='myown'):
    # Create a new role with api + list zones perms

    perms = ["USE_API"]
    if (perm):
        perms.append(perm)

    role = {'name': rolename, 'permissions': perms}
    url = '{}/role'.format(g['master-url'])
    assert requests.post(url, json=role, auth=auth, params={'wait': g['wait']}).status_code == 201

    # Create a new user using this role
    user={'id': str(uuid.uuid4()),
          'name': username,
          'roles': [rolename],
          'auth': {'password' : 'verysecret'}
          }
    uauth = (user['name'], user['auth']['password'])
    url = '{}/user'.format(g['master-url'])
    assert requests.post(url, json=user, auth=auth, params={'wait': g['wait']}).status_code == 201
    return (role, uauth)

def set_permission_on_role(g, role, auth, perms=["USE_API"], rolename='myown'):
    role['permissions'] = perms
    url = '{}/role/{}'.format(g['master-url'], role['name'])
    assert requests.put(url, json=role, auth=auth, params={'wait': g['wait']}).ok


def validate_permission(g, perm, fn, fnsetup=None, fnreset=None, fncleanup=None):
    td = get_second_tenant(g)
    auth=td['auth']['super']
    rval = True
    username = 'testperms'

    (role, uauth) = create_user_with_permission(g, perm, auth, username=username)

    if fnsetup:
        fnsetup(auth)

    # Validate that the user can do what we expect
    if not fn(uauth).ok:
        rval = False

    set_permission_on_role(g, role, auth)

    if fnreset:
         fnreset(auth)

    # Validate that the user can mno longer do what we expect
    if fn(uauth).status_code != 403:
        rval = False

    # Delete the role
    url = '{}/role/{}'.format(g['master-url'], role['name'])
    assert requests.delete(url, auth=auth, params={'wait': g['wait']}).ok

    # Delete user
    url = '{}/user/{}'.format(g['master-url'], username)
    assert requests.delete(url, auth=auth, params={'wait': g['wait']}).ok

    if fncleanup:
        fncleanup(auth)

    return rval

def test_perms_list_zones(global_data):
    assert validate_permission(global_data, 'LIST_ZONES', lambda auth : list_something(global_data, 'zone', auth))

def test_perms_get_zone(global_data):
    zone = 'test01.example.com'
    assert validate_permission(global_data, 'READ_ZONE', lambda auth : get_something(global_data, 'zone', zone, auth),
                               fnsetup=lambda auth : create_zone(global_data, zone, auth),
                               fncleanup=lambda auth : requests.delete('{}/zone/{}'.format(global_data['master-url'], zone), auth=auth))


def test_perms_create_zone(global_data):
    zone = 'test01.example.com'
    assert validate_permission(global_data, 'CREATE_ZONE', 
                               lambda auth : create_zone(global_data, zone, auth),
                               fnreset=lambda auth : requests.delete('{}/zone/{}'.format(global_data['master-url'], zone), auth=auth))
    
def test_perms_delete_zone(global_data):
    zone = 'test01.example.com'
    assert validate_permission(global_data, 'DELETE_ZONE', 
                               lambda auth : requests.delete('{}/zone/{}'.format(global_data['master-url'], zone), auth=auth),
                               fnsetup=lambda auth : create_zone(global_data, zone, auth),
                               fnreset=lambda auth : create_zone(global_data, zone, auth),
                               fncleanup=lambda auth : requests.delete('{}/zone/{}'.format(global_data['master-url'], zone), auth=auth))

def test_perms_get_rr(global_data):
    fqdn = "rr.tenant-1.com"
    assert validate_permission(global_data, 'READ_RR', 
                               lambda auth : get_something(global_data, 'rr', fqdn, auth),
                               fnsetup=lambda auth : create_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth),
                               fncleanup=lambda auth : delete_something(global_data, 'rr', fqdn, auth))
    
def test_perms_create_rr(global_data):
    fqdn = "rr.tenant-1.com"
    assert validate_permission(global_data, 'CREATE_RR', 
                               lambda auth : create_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth),
                               fnreset=lambda auth : delete_something(global_data, 'rr', fqdn, auth))
    
def test_perms_put_new_rr(global_data):
    fqdn = "rr.tenant-1.com"
    assert validate_permission(global_data, 'CREATE_RR', 
                               lambda auth : put_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth),
                               fnreset=lambda auth : delete_something(global_data, 'rr', fqdn, auth))
    
def test_perms_put_new_rr_with_update_perms(global_data):
    fqdn = "rr.tenant-1.com"
    assert not validate_permission(global_data, 'UPDATE_RR', 
                               lambda auth : put_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth))
    
def test_perms_put_rr(global_data):
    fqdn = "rr.tenant-1.com"
    assert validate_permission(global_data, 'UPDATE_RR', 
                               lambda auth : put_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth),
                               fnsetup=lambda auth : create_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth),
                               fncleanup=lambda auth : delete_something(global_data, 'rr', fqdn, auth))
    
def test_perms_patch_new_rr(global_data):
    fqdn = "rr.tenant-1.com"
    assert validate_permission(global_data, 'CREATE_RR', 
                               lambda auth : patch_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth),
                               fnreset=lambda auth : delete_something(global_data, 'rr', fqdn, auth))
    
def test_perms_patch_new_rr_with_update_perms(global_data):
    fqdn = "rr.tenant-1.com"
    assert not validate_permission(global_data, 'UPDATE_RR', 
                               lambda auth : patch_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth))
    
def test_perms_patch_rr(global_data):
    fqdn = "rr.tenant-1.com"
    assert validate_permission(global_data, 'UPDATE_RR', 
                               lambda auth : patch_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth),
                               fnsetup=lambda auth : create_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth),
                               fncleanup=lambda auth : delete_something(global_data, 'rr', fqdn, auth))
    
def test_perms_delete_rr(global_data):
    fqdn = "rr.tenant-1.com"
    assert validate_permission(global_data, 'DELETE_RR', 
                               lambda auth : delete_something(global_data, 'rr', fqdn, auth),
                               fnsetup=lambda auth : create_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth))

def test_perms_delete_rr_with_update_perms(global_data):
    fqdn = "rr.tenant-1.com"
    assert not validate_permission(global_data, 'UPDATE_RR', 
                               lambda auth : delete_something(global_data, 'rr', fqdn, auth),
                               fnsetup=lambda auth : create_something(global_data, 'rr', fqdn, {'a':['127.0.0.1']}, auth),
                               fncleanup=lambda auth : delete_something(global_data, 'rr', fqdn, auth))


def test_list_tenants_not_allowed(global_data):
    it =  iter(global_data['tenants'].items())
    tname, td = next(it)
    auth=td['auth']['super']
    assert list_something(global_data, 'tenant', auth).status_code == 403


def test_perms_get_self_tenant(global_data):
    td = get_second_tenant(global_data)
    tenant_id = td['id']
    assert validate_permission(global_data, 'GET_SELF_TENANT', 
                               lambda auth : get_something(global_data, 'tenant', tenant_id, auth))


@pytest.mark.skip(reason="Not implemented yet.")
def test_perms_patch_self_tenant(global_data):
    td = get_second_tenant(global_data)
    tenant_id = td['id']
    assert validate_permission(global_data, 'UPDATE_SELF_TENANT', 
                               lambda auth : patch_something(global_data, 'tenant', tenant_id, {'properties': [{'key': 'test', 'value': 'ok'}]}, auth))


def test_perms_delete_self_tenant(global_data):
    # create tenant
    tenant = create_default_tenant(global_data, 'deltenant')
    tauth = tenant['auth']['super']
    tid = tenant['id']

    # create user without perms
    (role, uauth) = create_user_with_permission(global_data, None, tauth)
    
    # try to delete tenant
    assert delete_something(global_data, "tenant", tid, uauth).status_code == 403

    # add delete self tenant to role
    set_permission_on_role(global_data, role, tauth, perms=["USE_API", "DELETE_SELF_TENANT"])

    # delete tenant
    assert delete_something(global_data, "tenant", tid, uauth).ok
