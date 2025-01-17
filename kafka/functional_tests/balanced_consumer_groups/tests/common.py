from typing import Dict
from typing import List

CONSUME_BASE_ROUTE = '/consume'
CONSUMERS = ['kafka-consumer-first', 'kafka-consumer-second']
TOPIC = 'test-topic'


async def start_consumers(
    service_client,
    consumers: List[str] = CONSUMERS,
) -> None:
    for consumer in consumers:
        response = await service_client.put(f'{CONSUME_BASE_ROUTE}/{consumer}')
        assert response.status_code == 200


async def stop_consumers(
    service_client,
    consumers: List[str] = CONSUMERS,
) -> None:
    for consumer in consumers:
        response = await service_client.delete(
            f'{CONSUME_BASE_ROUTE}/{consumer}',
        )
        assert response.status_code == 200


async def get_consumed_messages(
    service_client,
    consumer: str,
) -> List[Dict[str, str]]:
    response = await service_client.post(f'{CONSUME_BASE_ROUTE}/{consumer}')
    assert response.status_code == 200

    return response.json()['messages']


def parse_message_keys(messages: List[Dict[str, str]]) -> List[str]:
    return list(map(lambda message: message['key'], messages))


async def make_non_faulty(service_client, consumer) -> None:
    response = await service_client.patch(f'{CONSUME_BASE_ROUTE}/{consumer}')
    assert response.status_code == 200
