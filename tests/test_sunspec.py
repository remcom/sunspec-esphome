"""
Integration tests -- require the device to be flashed and reachable.
Run: pytest tests/test_sunspec.py --device-ip=<IP>
"""
import pytest
from pymodbus.client import ModbusTcpClient


def pytest_addoption(parser):
    parser.addoption("--device-ip", default=None, help="ESP32 device IP address")


@pytest.fixture(scope="module")
def client(request):
    ip = request.config.getoption("--device-ip")
    if ip is None:
        pytest.skip("--device-ip not provided")
    c = ModbusTcpClient(ip, port=502)
    c.connect()
    yield c
    c.close()


def test_sunspec_marker(client):
    """SunSpec marker 'SunS' must be at 40000-40001."""
    rr = client.read_holding_registers(40000, 2, slave=1)
    assert not rr.isError()
    assert rr.registers == [0x5375, 0x6E53], f"Got {[hex(r) for r in rr.registers]}"


def test_model1_id(client):
    """Common block model ID must be 1."""
    rr = client.read_holding_registers(40002, 1, slave=1)
    assert not rr.isError()
    assert rr.registers[0] == 1


def test_model101_id(client):
    """Inverter block model ID must be 101."""
    rr = client.read_holding_registers(40070, 1, slave=1)
    assert not rr.isError()
    assert rr.registers[0] == 101


def test_model120_id(client):
    """Nameplate block model ID must be 120."""
    rr = client.read_holding_registers(40122, 1, slave=1)
    assert not rr.isError()
    assert rr.registers[0] == 120


def test_model123_id(client):
    """Controls block model ID must be 123."""
    rr = client.read_holding_registers(40150, 1, slave=1)
    assert not rr.isError()
    assert rr.registers[0] == 123


def test_end_marker(client):
    """End marker at 40176 must be 0xFFFF."""
    rr = client.read_holding_registers(40176, 4, slave=1)
    assert not rr.isError()
    assert all(r == 0xFFFF for r in rr.registers)


def test_wmaxlimpct_default(client):
    """WMaxLimPct must default to 100."""
    rr = client.read_holding_registers(40155, 1, slave=1)
    assert not rr.isError()
    assert rr.registers[0] == 100


def test_wmaxlim_ena_default(client):
    """WMaxLim_Ena must default to 0."""
    rr = client.read_holding_registers(40159, 1, slave=1)
    assert not rr.isError()
    assert rr.registers[0] == 0


def test_scale_factors(client):
    """Scale factor registers must have expected values."""
    expected = {
        40076: 0xFFFE,  # -2 as uint16
        40083: 0xFFFF,  # -1 as uint16
        40085: 0,
        40087: 0xFFFE,  # -2 as uint16
        40096: 0,
        40107: 0xFFFF,  # -1 as uint16
    }
    for addr, val in expected.items():
        rr = client.read_holding_registers(addr, 1, slave=1)
        assert not rr.isError()
        assert rr.registers[0] == val, \
            f"Register {addr}: expected {hex(val)}, got {hex(rr.registers[0])}"


def test_fc03_reads_manufacturer(client):
    """FC03 should return manufacturer string from registers 40004-40019."""
    rr = client.read_holding_registers(40004, 16, slave=1)
    assert not rr.isError()
    raw = bytes(b for r in rr.registers for b in (r >> 8, r & 0xFF))
    assert raw.startswith(b"Solis"), f"Got: {raw}"


def test_fc03_out_of_range(client):
    """FC03 on address > 40199 must return exception code 0x02."""
    rr = client.read_holding_registers(40200, 1, slave=1)
    assert rr.isError() or rr.function_code == 0x83


def test_fc03_too_many_registers(client):
    """FC03 requesting > 120 registers must return exception code 0x03."""
    rr = client.read_holding_registers(40000, 121, slave=1)
    assert rr.isError() or rr.function_code == 0x83
