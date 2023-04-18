# This test assumes that the server is just bootstrapped and contains no zones.
# For example:
#    rm -rf /tmp/nsblast; LD_LIBRARY_PATH=/opt/boost_1_81_0/stage/lib ./bin/nsblast --db-path /tmp/nsblast --dns-udp-port 5353 --http-port 8080 -l trace --dns-endpoint 127.0.0.1


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
