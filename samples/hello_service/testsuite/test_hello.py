# /// [Functional test]
async def test_hello_base(service_client):
    response = await service_client.get('/hello')
    assert response.status == 200
    assert 'text/plain' in response.headers['Content-Type']
    assert response.text == 'Hello, unknown user!\n'
    assert 'X-RequestId' not in response.headers.keys(), 'Unexpected header'

    response = await service_client.get('/hello', params={'name': 'userver'})
    assert response.status == 200
    assert 'text/plain' in response.headers['Content-Type']
    assert response.text == 'Hello, userver!\n'
    # /// [Functional test]


async def test_hello_head(service_client):
    response = await service_client.request('HEAD', '/hello')
    assert response.status == 200
    assert 'text/plain' in response.headers['Content-Type']
    assert response.text == ''
    assert 'X-RequestId' not in response.headers.keys(), 'Unexpected header'


async def test_wrong_method(service_client):
    response = await service_client.request('KEK', '/hello')
    assert response.status == 400
    assert response.text == 'bad request'
    assert 'X-YaRequestId' not in response.headers.keys(), 'Unexpected header'
