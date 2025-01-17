"""
Plugin that imports the required fixtures to start the broker.
"""

import json
import logging
import os

import pytest

from testsuite.databases.kafka.classes import BootstrapServers

pytest_plugins = [
    'testsuite.databases.kafka.pytest_plugin',
    'pytest_userver.plugins.core',
]


@pytest.fixture(scope='session')
def _patched_bootstrap_servers_internal() -> BootstrapServers:
    """Used for internal testing purposes"""

    brokers_list = os.getenv('KAFKA_RECIPE_BROKER_LIST')
    if brokers_list:
        return brokers_list.split(',')

    return []


@pytest.fixture(scope='session')
def kafka_secdist(_bootstrap_servers, service_config) -> str:
    """
    Automatically generates secdist config from user static config.
    `_bootstrap_servers` is testsuite's fixture that determines current
    bootstrap servers list depends on Kafka testsuite plugin's settings.

    @snippet samples/kafka_service/testsuite/conftest.py  Kafka service sample - secdist

    @ingroup userver_testsuite_fixtures
    """

    single_setting = {
        'brokers': _bootstrap_servers,
        'username': '',
        'password': '',
    }
    logging.info(f'Kafka brokers are: {single_setting["brokers"]}')

    secdist_config = {}
    secdist_config['kafka_settings'] = {}

    components = service_config['components_manager']['components']
    for component_name in components:
        is_kafka_producer = component_name.startswith('kafka-producer')
        is_kafka_consumer = component_name.startswith('kafka-consumer')
        if is_kafka_producer or is_kafka_consumer:
            secdist_config['kafka_settings'][component_name] = single_setting

    return json.dumps(secdist_config)
