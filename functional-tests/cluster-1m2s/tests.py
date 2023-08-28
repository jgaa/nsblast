# This test is designed to be run with run.sh in this folder.
#
# example: bash run.sh
#

import requests
import yaml
import json
import pytest
import dns.resolver
import time
import os

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
            'master-url': 'http://127.0.0.1:8080/api/v1',
            'slave1-url': 'http://127.0.0.1:8081/api/v1',
            'slave2-url': 'http://127.0.0.1:8082/api/v1',
            'pass': password
           }


def test_create_zone(global_data):
    zone = """
      ttl: 1000
      soa:
        refresh: 2000
        retry: 3000
        expire: 4000
        minimum: 5000
        mname: master
        rname: hostmaster.example.com
      ns:
        - master
        - slave1
        - slave2
      a:
        - 127.0.0.1
        - 127.0.0.2
      mx:
        - priority: 10
          host: mail.example.com
    """

    print('Creating example.com zone')
    url = global_data['master-url'] + '/zone/example.com'
    body = yaml.load(zone, Loader=yaml.Loader)
    r = requests.post(url, json=body, auth=('admin', global_data['pass']), params={'wait': 30})
    print(r.text)
    assert r.ok

def test_check_zone_on_master(global_data):
    dns = global_data['master-dns']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000

    soa = answer.rrset[0];
    assert soa.rdtype == 6
    assert soa.serial == 1

def test_check_zone_on_slave1(global_data):
    dns = global_data['slave1']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000
    soa = answer.rrset[0];
    assert soa.rdtype == 6
    assert soa.serial == 1


def test_check_zone_on_slave2(global_data):
    dns = global_data['slave2']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000
    soa = answer.rrset[0];
    assert soa.rdtype == 6
    assert soa.serial == 1


def test_create_www(global_data):
    entry = """
      a:
        - 127.0.0.3
        - 127.0.0.4
    """

    print('Creating www.example.com A entry')
    url = global_data['master-url'] + '/rr/www.example.com'
    body = yaml.load(entry, Loader=yaml.Loader)
    r = requests.post(url, json=body, auth=('admin', global_data['pass']), params={'wait': 30})
    print(r.text)
    assert r.ok


def test_check_zone_on_master_after_update(global_data):
    dns = global_data['master-dns']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000
    soa = answer.rrset[0];
    assert soa.rdtype == 6
    assert soa.serial == 2


def test_check_zone_on_slave2_after_update(global_data):
    dns = global_data['slave2']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000
    soa = answer.rrset[0];
    assert soa.rdtype == 6
    assert soa.serial == 2


def test_check_zone_on_slave1_after_update(global_data):
    dns = global_data['slave1']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000
    soa = answer.rrset[0];
    assert soa.rdtype == 6
    assert soa.serial == 2


def test_create_TeSt1(global_data):
    entry = """
      ttl: 3600
      a:
        - 127.0.0.3
        - 127.0.0.4
    """

    url = global_data['master-url'] + '/rr/TeSt1.example.com'
    body = yaml.load(entry, Loader=yaml.Loader)
    r = requests.post(url, json=body, auth=('admin', global_data['pass']), params={'wait': 30})
    print(r.text)
    assert r.ok


def test_query_soa(global_data):
    dns = global_data['master-dns']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000

    soa = answer.rrset[0];
    assert soa.rdtype == 6


def test_query_soaCase(global_data):
    dns = global_data['master-dns']
    answer = dns.resolve('Example.CoM', 'SOA')
    assert answer.rrset.ttl == 1000

    soa = answer.rrset[0];
    assert soa.rdtype == 6 # SOA


def test_query_test1Nocase(global_data):
    dns = global_data['master-dns']
    answer = dns.resolve('test1.example.com', 'A')
    assert answer.rrset.ttl == 3600

    a = answer.rrset[0];
    assert a.rdtype == 1 # A
    assert answer.name.to_text(True) == 'TeSt1.example.com'
    assert answer.name.to_text(True) != 'test1.example.com'


def test_query_test1Nocase_slave2(global_data):
    time.sleep(3)
    dns = global_data['slave2']
    answer = dns.resolve('test1.example.com', 'A')
    assert answer.rrset.ttl == 3600

    a = answer.rrset[0];
    assert a.rdtype == 1 # A
    assert answer.name.to_text(True) == 'TeSt1.example.com'
    assert answer.name.to_text(True) != 'test1.example.com'


def test_query_test1Nocase_slave1(global_data):
    time.sleep(5)
    dns = global_data['slave1']
    answer = dns.resolve('test1.example.com', 'A')
    assert answer.rrset.ttl == 3600

    a = answer.rrset[0];
    assert a.rdtype == 1 # A
    assert answer.name.to_text(True) == 'TeSt1.example.com'
    assert answer.name.to_text(True) != 'test1.example.com'


def test_nonexistingLookup(global_data):
    dns = global_data['master-dns']

    with pytest.raises(Exception):
        dns.resolve('dontexist.example.com', 'A')


def test_nonexistingLookup_ixfr(global_data):
    dns = global_data['slave2']

    with pytest.raises(Exception):
        dns.resolve('dontexist.example.com', 'A')


def test_nonexistingLookup_axfr(global_data):
    dns = global_data['slave1']

    with pytest.raises(Exception):
        dns.resolve('dontexist.example.com', 'A')


def test_zero_ttl(global_data):
    entry = """
      ttl: 0
      a:
        - 127.0.0.5
        - 127.0.0.6
    """

    print('Creating zero.example.com A entry')
    url = global_data['master-url'] + '/rr/zero.example.com'
    body = yaml.load(entry, Loader=yaml.Loader)
    r = requests.post(url, json=body, auth=('admin', global_data['pass']), params={'wait': 30})
    print(r.text)
    assert r.ok

    dns = global_data['master-dns']
    answer = dns.resolve('zero.example.com', 'A')
    assert answer.rrset.ttl == 0

