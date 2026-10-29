"""End-to-end tests for relay.py using plain UDP sockets that mimic the C++ RelayClient."""
import asyncio
import os
import socket
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import relay as R  # noqa: E402

LOCAL = "127.0.0.1"


class Udp:
    def __init__(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind((LOCAL, 0))
        self.s.setblocking(False)
        self.port = self.s.getsockname()[1]

    def send(self, data: bytes, port: int):
        self.s.sendto(data, (LOCAL, port))

    async def recv(self, timeout=1.0):
        loop = asyncio.get_event_loop()
        return await asyncio.wait_for(loop.sock_recvfrom(self.s, 2048), timeout)

    def close(self):
        self.s.close()


def enc_create(max_guests=7, ver=0x100):
    return R.header(R.OP_CREATE) + struct.pack("<BI", max_guests, ver)


def enc_join(code: str, ver=0x100):
    return R.header(R.OP_JOIN) + code.encode() + struct.pack("<I", ver)


def enc_ping(token: int, slot: int):
    return R.header(R.OP_PING) + struct.pack("<QB", token, slot)


def dec_created(d: bytes):
    assert d[:6] == R.header(R.OP_CREATED), d
    code = d[6:12].decode()
    host_token, host_port, n = struct.unpack("<QHB", d[12:23])
    ports = list(struct.unpack("<%dH" % n, d[23:23 + 2 * n]))
    return code, host_token, host_port, ports


def dec_joined(d: bytes):
    assert d[:6] == R.header(R.OP_JOINED), d
    return struct.unpack("<HBQ", d[6:17])


@pytest.fixture
async def relay():
    r = R.Relay(control_port=0, port_range=(0, 0), room_idle_s=30, guest_idle_s=30, bind=LOCAL)
    # Use ephemeral ports: patch allocation to bind port 0 and read back the real port.
    r.ports_free = [0] * 40
    orig_open = r._open_room_sockets

    async def open_with_ephemeral(room, reply_to):
        loop = asyncio.get_event_loop()
        room.host_transport, _ = await loop.create_datagram_endpoint(
            lambda: R._HostPortProtocol(r, room), local_addr=(LOCAL, 0))
        room.host_port = room.host_transport.get_extra_info("sockname")[1]
        for slot in room.slots:
            slot.transport, _ = await loop.create_datagram_endpoint(
                lambda s=slot: R._GuestPortProtocol(r, room, s), local_addr=(LOCAL, 0))
            slot.port = slot.transport.get_extra_info("sockname")[1]
        r._send_control(R.enc_created(room.code, room.host_token, room.host_port, [s.port for s in room.slots]), reply_to)

    r._open_room_sockets = open_with_ephemeral  # type: ignore[assignment]
    await r.start()
    r.control_port = r.control_transport.get_extra_info("sockname")[1]
    yield r
    await r.stop()


async def create_room(relay, host: Udp, max_guests=2):
    host.send(enc_create(max_guests), relay.control_port)
    d, _ = await host.recv()
    return dec_created(d)


async def learn_mappings(host: Udp, host_token: int, guest_ports, guest: Udp, slot_token: int, slot: int, host_port: int):
    for i, p in enumerate(guest_ports, start=1):
        host.send(enc_ping(host_token, i), p)
        d, _ = await host.recv()
        assert d[:6] == R.header(R.OP_PONG)
    guest.send(enc_ping(slot_token, slot), host_port)
    d, _ = await guest.recv()
    assert d[:6] == R.header(R.OP_PONG)


@pytest.mark.asyncio
async def test_create_join_forward_both_ways(relay):
    host, guest = Udp(), Udp()
    code, host_token, host_port, guest_ports = await create_room(relay, host)
    assert len(code) == 6 and all(c in R.ROOM_CODE_ALPHABET for c in code)
    assert len(guest_ports) == 2

    guest.send(enc_join(code), relay.control_port)
    d, _ = await guest.recv()
    j_host_port, slot, slot_token = dec_joined(d)
    assert j_host_port == host_port and slot == 1

    await learn_mappings(host, host_token, guest_ports, guest, slot_token, slot, host_port)

    # Opaque "ENet" payload guest -> host arrives from relay:guest_port[0]
    guest.send(b"\x00\x01ENET-FROM-GUEST", host_port)
    d, frm = await host.recv()
    assert d == b"\x00\x01ENET-FROM-GUEST" and frm[1] == guest_ports[0]
    # host -> guest via guest port arrives from relay:host_port
    host.send(b"\x00\x02ENET-FROM-HOST", guest_ports[0])
    d, frm = await guest.recv()
    assert d == b"\x00\x02ENET-FROM-HOST" and frm[1] == host_port
    host.close(); guest.close()


@pytest.mark.asyncio
async def test_unknown_room_full_room_and_bad_token(relay):
    host, g1, g2, g3 = Udp(), Udp(), Udp(), Udp()
    g1.send(enc_join("ZZZZZZ"), relay.control_port)
    d, _ = await g1.recv()
    assert d[:6] == R.header(R.OP_ERROR) and d[6] == R.ERR_ROOM_NOT_FOUND

    code, host_token, host_port, guest_ports = await create_room(relay, host, max_guests=2)
    for g in (g1, g2):
        g.send(enc_join(code), relay.control_port)
        d, _ = await g.recv()
        dec_joined(d)
    g3.send(enc_join(code), relay.control_port)
    d, _ = await g3.recv()
    assert d[:6] == R.header(R.OP_ERROR) and d[6] == R.ERR_ROOM_FULL

    # Wrong token ping is rejected.
    g1.send(enc_ping(12345, 1), host_port)
    d, _ = await g1.recv()
    assert d[:6] == R.header(R.OP_ERROR) and d[6] == R.ERR_BAD_TOKEN
    for u in (host, g1, g2, g3):
        u.close()


@pytest.mark.asyncio
async def test_version_mismatch_and_close(relay):
    host, guest = Udp(), Udp()
    code, host_token, host_port, guest_ports = await create_room(relay, host)
    guest.send(enc_join(code, ver=0x999), relay.control_port)
    d, _ = await guest.recv()
    assert d[:6] == R.header(R.OP_ERROR) and d[6] == R.ERR_VERSION

    host.send(R.header(R.OP_CLOSE) + struct.pack("<Q", host_token), relay.control_port)
    await asyncio.sleep(0.05)
    assert code not in relay.rooms
    guest.send(enc_join(code), relay.control_port)
    d, _ = await guest.recv()
    assert d[6] == R.ERR_ROOM_NOT_FOUND
    host.close(); guest.close()


@pytest.mark.asyncio
async def test_packets_dropped_until_mappings_known(relay):
    host, guest = Udp(), Udp()
    code, host_token, host_port, guest_ports = await create_room(relay, host)
    guest.send(enc_join(code), relay.control_port)
    d, _ = await guest.recv()
    _, slot, slot_token = dec_joined(d)
    # Guest sends data before pinging: relay must not forward (no mapping) and must not crash.
    guest.send(b"\x00\x00early", host_port)
    with pytest.raises(asyncio.TimeoutError):
        await host.recv(timeout=0.2)
    await learn_mappings(host, host_token, guest_ports, guest, slot_token, slot, host_port)
    guest.send(b"\x00\x00late", host_port)
    d, _ = await host.recv()
    assert d == b"\x00\x00late"
    host.close(); guest.close()
