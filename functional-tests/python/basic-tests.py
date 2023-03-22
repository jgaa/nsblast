# This test assumes that the server is just bootstrapped and contains no zones.
# For example:
#    rm -rf /tmp/nsblast; LD_LIBRARY_PATH=/opt/boost_1_81_0/stage/lib ./bin/nsblast --db-path /tmp/nsblast --dns-udp-port 5353 --http-port 8080 -l trace --dns-endpoint 127.0.0.1


import requests
import yaml
import pytest
import pprint
import dns.resolver

@pytest.fixture(scope = 'module')
def global_data():
    resolver = dns.resolver.Resolver(configure=False)
    resolver.nameservers = ['127.0.0.1']
    resolver.port = 5353

    return {'dns': resolver,
            'url': 'http://127.0.0.1:8080/api/v1'
           }

@pytest.mark.order(1)
def test_create_zone(global_data):
    zone = """
      ttl: 1000
      soa:
        refresh: 2000
        retry: 3000
        expire: 4000
        minimum: 5000
        mname: ns1.example.com
        rname: hostmaster.example.com
      ns:
        - ns1.example.com
        - ns2.example.com
      a:
        - 127.0.0.1
        - 127.0.0.2
      mx:
        - priority: 10
          host: mail.example.com
    """

    print('Creating example.com zone')
    url = global_data['url'] + '/zone/example.com'
    body = yaml.load(zone, Loader=yaml.Loader)
    r = requests.post(url, json=body)
    print(r.text)
    assert r.ok

@pytest.mark.order(2)
def test_create_www(global_data):
    entry = """
      a:
        - 127.0.0.3
        - 127.0.0.4
    """

    print('Creating www.example.com A entry')
    url = global_data['url'] + '/rr/www.example.com'
    body = yaml.load(entry, Loader=yaml.Loader)
    r = requests.post(url, json=body)
    print(r.text)
    assert r.ok

@pytest.mark.order(2)
def test_create_TeSt1(global_data):
    entry = """
      ttl: 3600
      a:
        - 127.0.0.3
        - 127.0.0.4
    """

    print('Creating www.example.com A entry')
    url = global_data['url'] + '/rr/TeSt1.example.com'
    body = yaml.load(entry, Loader=yaml.Loader)
    r = requests.post(url, json=body)
    print(r.text)
    assert r.ok

@pytest.mark.order(3)
def test_query_soa(global_data):
    dns = global_data['dns']
    answer = dns.resolve('example.com', 'SOA')
    assert answer.rrset.ttl == 1000

    soa = answer.rrset[0];
    assert soa.rdtype == 6

@pytest.mark.order(3)
def test_query_soaCase(global_data):
    dns = global_data['dns']
    answer = dns.resolve('Example.CoM', 'SOA')
    assert answer.rrset.ttl == 1000

    soa = answer.rrset[0];
    assert soa.rdtype == 6 # SOA

@pytest.mark.order(3)
def test_query_test1Nocase(global_data):
    dns = global_data['dns']
    answer = dns.resolve('test1.example.com', 'A')
    assert answer.rrset.ttl == 3600

    a = answer.rrset[0];
    assert a.rdtype == 1 # A
    assert answer.name.to_text(True) == 'TeSt1.example.com'
    assert answer.name.to_text(True) != 'test1.example.com'

@pytest.mark.order(3)
def test_nonexistingLookup(global_data):
    dns = global_data['dns']

    with pytest.raises(Exception):
        dns.resolve('dontexist.example.com', 'A')


@pytest.mark.order(3)
def test_query_test1Axfr(global_data):

    z = dns.zone.from_xfr(dns.query.xfr('127.0.0.1', 'example.com', lifetime=1000, port=5353))

    print(z.to_text())

    assert z.to_text() == """@ 1000 IN SOA ns1 hostmaster 3 2000 3000 4000 5000
@ 1000 IN NS ns1
@ 1000 IN NS ns2
@ 1000 IN A 127.0.0.1
@ 1000 IN A 127.0.0.2
@ 1000 IN MX 10 mail
TeSt1 3600 IN A 127.0.0.3
TeSt1 3600 IN A 127.0.0.4
www 2592000 IN A 127.0.0.3
www 2592000 IN A 127.0.0.4
"""

def test_zero_ttl(global_data):
    entry = """
      ttl: 0
      a:
        - 127.0.0.5
        - 127.0.0.6
    """

    print('Creating zero.example.com A entry')
    url = global_data['url'] + '/rr/zero.example.com'
    body = yaml.load(entry, Loader=yaml.Loader)
    r = requests.post(url, json=body)
    print(r.text)
    assert r.ok

    dns = global_data['dns']
    answer = dns.resolve('zero.example.com', 'A')
    assert answer.rrset.ttl == 0
