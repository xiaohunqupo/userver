import pytest

pytest_plugins = ['pytest_userver.plugins.core']


# Overriding a userver fixture
@pytest.fixture(name='userver_config_testsuite', scope='session')
def _userver_config_testsuite(userver_config_testsuite):
    def patch_config(config, config_vars) -> None:
        userver_config_testsuite(config, config_vars)
        config['components_manager']['graceful_shutdown_interval'] = '3s'

    return patch_config
