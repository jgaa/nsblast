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


@pytest.fixture(scope = 'module')
def global_data():
    mresolver = dns.resolver.Resolver(configure=False)
    mresolver.nameservers = ['127.0.0.1']
    mresolver.port = 5354

    slave_axfr = dns.resolver.Resolver(configure=False)
    slave_axfr.nameservers = ['127.0.0.1']
    slave_axfr.port = 5355

    slave_ixfr = dns.resolver.Resolver(configure=False)
    slave_ixfr.nameservers = ['127.0.0.1']
    slave_ixfr.port = 5356

    return {'master-dns': mresolver,
            'slave-axfr': slave_axfr,
            'slave-ixfr': slave_ixfr,
            'master-url': 'http://127.0.0.1:8080/api/v1',
            'slave-axfr-url': 'http://127.0.0.1:8081/api/v1',
            'slave-ixfr-url': 'http://127.0.0.1:8082/api/v1'
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
        - slave-axfr
        - slave-ixfr
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
    r = requests.post(url, json=body)
    print(r.text)
    assert r.ok

def test_check_zone_on_master(global_data):
    dns = global_data['master-dns']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000

    soa = answer.rrset[0];
    assert soa.rdtype == 6
    assert soa.serial == 1

def test_setup_axfr_slave(global_data):
    data = """
    {
        "master": {
            "hostname": "master",
            "port": 53,
            "refresh": 5,
            "strategy": "axfr"
        }
    }
    """
    url = global_data['slave-axfr-url'] + '/config/example.com/master'
    r = requests.post(url, json=json.loads(data))
    print(r.text)
    assert r.ok


def test_setup_ixfr_slave(global_data):
    data = """
    {
        "master": {
            "hostname": "master",
            "port": 53,
            "refresh": 60,
            "strategy": "ixfr"
        }
    }
    """
    url = global_data['slave-ixfr-url'] + '/config/example.com/master'
    r = requests.post(url, json=json.loads(data))
    print(r.text)
    assert r.ok


def test_check_zone_on_axfr_slave(global_data):
    time.sleep(3)
    dns = global_data['slave-axfr']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000
    soa = answer.rrset[0];
    assert soa.rdtype == 6
    assert soa.serial == 1


def test_check_zone_on_ixfr_slave(global_data):
    dns = global_data['slave-ixfr']
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
    r = requests.post(url, json=body)
    print(r.text)
    assert r.ok


def test_check_zone_on_master_after_update(global_data):
    dns = global_data['master-dns']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000
    soa = answer.rrset[0];
    assert soa.rdtype == 6
    assert soa.serial == 2


def test_check_zone_on_ixfr_slave_after_update(global_data):
    time.sleep(2)
    dns = global_data['slave-ixfr']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000
    soa = answer.rrset[0];
    assert soa.rdtype == 6
    assert soa.serial == 2


def test_check_zone_on_axfr_slave_after_update(global_data):
    time.sleep(5)
    dns = global_data['slave-axfr']
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
    r = requests.post(url, json=body)
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


def test_query_test1Nocase_slave_ixfr(global_data):
    time.sleep(3)
    dns = global_data['slave-ixfr']
    answer = dns.resolve('test1.example.com', 'A')
    assert answer.rrset.ttl == 3600

    a = answer.rrset[0];
    assert a.rdtype == 1 # A
    assert answer.name.to_text(True) == 'TeSt1.example.com'
    assert answer.name.to_text(True) != 'test1.example.com'


def test_query_test1Nocase_slave_axfr(global_data):
    time.sleep(5)
    dns = global_data['slave-axfr']
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
    dns = global_data['slave-ixfr']

    with pytest.raises(Exception):
        dns.resolve('dontexist.example.com', 'A')


def test_nonexistingLookup_axfr(global_data):
    dns = global_data['slave-axfr']

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
    r = requests.post(url, json=body)
    print(r.text)
    assert r.ok

    dns = global_data['master-dns']
    answer = dns.resolve('zero.example.com', 'A')
    assert answer.rrset.ttl == 0


def test_zero_ttl_ixfr(global_data):
    time.sleep(3)

    dns = global_data['slave-ixfr']
    answer = dns.resolve('zero.example.com', 'A')
    assert answer.rrset.ttl == 0


def test_zero_ttl_axfr(global_data):
    time.sleep(5)

    dns = global_data['slave-axfr']
    answer = dns.resolve('zero.example.com', 'A')
    assert answer.rrset.ttl == 0


def test_query_test1Axfr(global_data):
    z = dns.zone.from_xfr(dns.query.xfr('127.0.0.1', 'example.com', lifetime=1000, port=5354))

    print(z.to_text())

    assert z.to_text() == """@ 1000 IN SOA master. hostmaster 4 2000 3000 4000 5000
@ 1000 IN NS master.
@ 1000 IN NS slave-axfr.
@ 1000 IN NS slave-ixfr.
@ 1000 IN A 127.0.0.1
@ 1000 IN A 127.0.0.2
@ 1000 IN MX 10 mail
TeSt1 3600 IN A 127.0.0.3
TeSt1 3600 IN A 127.0.0.4
www 2592000 IN A 127.0.0.3
www 2592000 IN A 127.0.0.4
zero 0 IN A 127.0.0.5
zero 0 IN A 127.0.0.6
"""

def test_query_test1Axfr_ixfr(global_data):
    z = dns.zone.from_xfr(dns.query.xfr('127.0.0.1', 'example.com', lifetime=1000, port=5356))

    print(z.to_text())

    assert z.to_text() == """@ 1000 IN SOA master. hostmaster 4 2000 3000 4000 5000
@ 1000 IN NS master.
@ 1000 IN NS slave-axfr.
@ 1000 IN NS slave-ixfr.
@ 1000 IN A 127.0.0.1
@ 1000 IN A 127.0.0.2
@ 1000 IN MX 10 mail
TeSt1 3600 IN A 127.0.0.3
TeSt1 3600 IN A 127.0.0.4
www 2592000 IN A 127.0.0.3
www 2592000 IN A 127.0.0.4
zero 0 IN A 127.0.0.5
zero 0 IN A 127.0.0.6
"""

def test_query_test1Axfr_axfr(global_data):
    z = dns.zone.from_xfr(dns.query.xfr('127.0.0.1', 'example.com', lifetime=1000, port=5355))

    print(z.to_text())

    assert z.to_text() == """@ 1000 IN SOA master. hostmaster 4 2000 3000 4000 5000
@ 1000 IN NS master.
@ 1000 IN NS slave-axfr.
@ 1000 IN NS slave-ixfr.
@ 1000 IN A 127.0.0.1
@ 1000 IN A 127.0.0.2
@ 1000 IN MX 10 mail
TeSt1 3600 IN A 127.0.0.3
TeSt1 3600 IN A 127.0.0.4
www 2592000 IN A 127.0.0.3
www 2592000 IN A 127.0.0.4
zero 0 IN A 127.0.0.5
zero 0 IN A 127.0.0.6
"""
