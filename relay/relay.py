#!/usr/bin/env python3
"""DSCoop relay / hub — asyncio UDP, no third-party dependencies.

Design (see docs/relay.md): the relay is a transparent UDP address translator so ENet stays
untouched on both ends.

  * control port (default 27000): CREATE / JOIN / CLOSE requests (DSCP, see
    src/proto/include/dsproto/relay_control.hpp — this file mirrors that format byte for byte).
  * per room: 1 "host port" guests ENet-connect to, plus N "guest ports" the host sees guests on.
      guest -> relay:hostPort            ===>  relay:guestPort[slot] -> host
      host  -> relay:guestPort[slot]     ===>  relay:hostPort        -> guest
  * mappings are learned from DSCP PING packets carrying the room/slot token, so symmetric NATs
    work as long as each side keeps pinging (clients ping every 2 s).

Run:  python3 relay.py --control-port 27000 --port-range 27100-27999
"""
from __future__ import annotations

import argparse
import asyncio
import logging
import secrets
import struct
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

log = logging.getLogger("dsrelay")

MAGIC = b"DSCP"
VERSION = 1
OP_CREATE, OP_CREATED, OP_JOIN, OP_JOINED, OP_ERROR, OP_PING, OP_PONG, OP_CLOSE = range(1, 9)
ERR_BAD_REQUEST, ERR_ROOM_NOT_FOUND, ERR_ROOM_FULL, ERR_NO_CAPACITY, ERR_VERSION, ERR_BAD_TOKEN = range(1, 7)
ROOM_CODE_ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"
ROOM_CODE_LEN = 6
MAX_GUESTS = 7
Addr = Tuple[str, int]


def header(op: int) -> bytes:
    return MAGIC + bytes([VERSION, op])


def is_control(data: bytes) -> bool:
    return len(data) >= 6 and data[:4] == MAGIC and data[4] == VERSION


def enc_error(code: int, text: str = "") -> bytes:
    t = text.encode("utf-8")[:255]
    return header(OP_ERROR) + bytes([code, len(t)]) + t


def enc_created(code: str, host_token: int, host_port: int, guest_ports: List[int]) -> bytes:
    body = code.encode("ascii") + struct.pack("<QHB", host_token, host_port, len(guest_ports))
    body += b"".join(struct.pack("<H", p) for p in guest_ports)
    return header(OP_CREATED) + body


def enc_joined(host_port: int, slot: int, slot_token: int) -> bytes:
    return header(OP_JOINED) + struct.pack("<HBQ", host_port, slot, slot_token)


def enc_pong(slot: int, server_time_ms: int) -> bytes:
    return header(OP_PONG) + struct.pack("<BI", slot, server_time_ms & 0xFFFFFFFF)


def new_room_code() -> str:
    return "".join(secrets.choice(ROOM_CODE_ALPHABET) for _ in range(ROOM_CODE_LEN))


def now_ms() -> int:
    return int(time.monotonic() * 1000)


@dataclass
class Slot:
    """One guest seat: the guest's address as seen by hostPort, the host's address as seen by this guest port."""
    index: int                      # 1..N
    port: int                       # relay guest port the host talks to
    token: int = 0                  # slot token handed to the guest on JOIN
    guest_addr: Optional[Addr] = None
    host_addr: Optional[Addr] = None   # host NAT mapping towards this port (learned from host PING)
    guest_last_seen: float = 0.0
    host_last_seen: float = 0.0
    assigned_at: float = 0.0
    transport: Optional[asyncio.DatagramTransport] = None

    @property
    def assigned(self) -> bool:
        return self.token != 0


@dataclass
class Room:
    code: str
    host_token: int
    host_port: int
    created_at: float
    client_version: int
    slots: List[Slot] = field(default_factory=list)
    host_transport: Optional[asyncio.DatagramTransport] = None
    last_host_activity: float = 0.0
    forwarded_to_host: int = 0
    forwarded_to_guests: int = 0

    def slot_by_guest_addr(self, addr: Addr) -> Optional[Slot]:
        for s in self.slots:
            if s.guest_addr == addr:
                return s
        return None

    def free_slot(self) -> Optional[Slot]:
        for s in self.slots:
            if not s.assigned:
                return s
        return None


class Relay:
    def __init__(self, control_port: int, port_range: Tuple[int, int], room_idle_s: float, guest_idle_s: float,
                 bind: str = "0.0.0.0", require_version: Optional[int] = None):
        self.control_port = control_port
        self.bind = bind
        self.ports_free: List[int] = list(range(port_range[0], port_range[1] + 1))
        self.room_idle_s = room_idle_s
        self.guest_idle_s = guest_idle_s
        self.require_version = require_version
        self.rooms: Dict[str, Room] = {}
        self.rooms_by_token: Dict[int, Room] = {}
        self.loop = asyncio.get_event_loop()
        self.control_transport: Optional[asyncio.DatagramTransport] = None
        self.stats = {"rooms_created": 0, "joins": 0, "datagrams": 0, "errors": 0}

    # ------------------------------------------------------------------ lifecycle
    async def start(self) -> None:
        self.control_transport, _ = await self.loop.create_datagram_endpoint(
            lambda: _ControlProtocol(self), local_addr=(self.bind, self.control_port))
        log.info("control port udp/%d, %d relay ports available (%d rooms max)", self.control_port,
                 len(self.ports_free), len(self.ports_free) // (1 + MAX_GUESTS))
        self.loop.create_task(self._reaper())

    async def stop(self) -> None:
        for room in list(self.rooms.values()):
            self._close_room(room, "shutdown")
        if self.control_transport:
            self.control_transport.close()

    async def _reaper(self) -> None:
        while True:
            await asyncio.sleep(5)
            now = time.monotonic()
            for room in list(self.rooms.values()):
                if now - max(room.last_host_activity, room.created_at) > self.room_idle_s:
                    self._close_room(room, "host idle")
                    continue
                for s in room.slots:
                    if s.assigned and now - max(s.guest_last_seen, s.assigned_at) > self.guest_idle_s:
                        log.info("room %s slot %d released (guest idle)", room.code, s.index)
                        s.token = 0
                        s.guest_addr = None

    # ------------------------------------------------------------------ control
    def on_control(self, data: bytes, addr: Addr) -> None:
        self.stats["datagrams"] += 1
        if not is_control(data):
            return
        op = data[5]
        body = data[6:]
        try:
            if op == OP_CREATE:
                self._handle_create(body, addr)
            elif op == OP_JOIN:
                self._handle_join(body, addr)
            elif op == OP_CLOSE:
                self._handle_close(body, addr)
            else:
                self._send_control(enc_error(ERR_BAD_REQUEST, "unexpected op on control port"), addr)
        except struct.error:
            self.stats["errors"] += 1
            self._send_control(enc_error(ERR_BAD_REQUEST, "malformed"), addr)

    def _send_control(self, data: bytes, addr: Addr) -> None:
        if self.control_transport:
            self.control_transport.sendto(data, addr)

    def _check_version(self, client_version: int, addr: Addr) -> bool:
        if self.require_version is not None and client_version != self.require_version:
            self._send_control(enc_error(ERR_VERSION, f"relay requires client version {self.require_version:#x}"), addr)
            return False
        return True

    def _handle_create(self, body: bytes, addr: Addr) -> None:
        max_guests, client_version = struct.unpack("<BI", body[:5])
        if not self._check_version(client_version, addr):
            return
        max_guests = max(1, min(MAX_GUESTS, max_guests))
        # Retransmit tolerance: same address asking again within 10 s gets the same room.
        for room in self.rooms.values():
            if room.host_transport is not None and getattr(room, "_creator", None) == addr and time.monotonic() - room.created_at < 10:
                self._send_control(enc_created(room.code, room.host_token, room.host_port, [s.port for s in room.slots]), addr)
                return
        needed = 1 + max_guests
        if len(self.ports_free) < needed:
            self.stats["errors"] += 1
            self._send_control(enc_error(ERR_NO_CAPACITY), addr)
            return
        ports = [self.ports_free.pop(0) for _ in range(needed)]
        code = new_room_code()
        while code in self.rooms:
            code = new_room_code()
        room = Room(code=code, host_token=secrets.randbits(64) or 1, host_port=ports[0], created_at=time.monotonic(),
                    client_version=client_version)
        room._creator = addr  # type: ignore[attr-defined]
        room.last_host_activity = room.created_at
        for i, p in enumerate(ports[1:], start=1):
            room.slots.append(Slot(index=i, port=p))
        self.rooms[code] = room
        self.rooms_by_token[room.host_token] = room
        self.stats["rooms_created"] += 1
        self.loop.create_task(self._open_room_sockets(room, addr))

    async def _open_room_sockets(self, room: Room, reply_to: Addr) -> None:
        try:
            room.host_transport, _ = await self.loop.create_datagram_endpoint(
                lambda: _HostPortProtocol(self, room), local_addr=(self.bind, room.host_port))
            for slot in room.slots:
                slot.transport, _ = await self.loop.create_datagram_endpoint(
                    lambda s=slot: _GuestPortProtocol(self, room, s), local_addr=(self.bind, slot.port))
        except OSError as e:
            log.error("cannot bind room ports: %s", e)
            self._close_room(room, "bind failed")
            self._send_control(enc_error(ERR_NO_CAPACITY, "port bind failed"), reply_to)
            return
        log.info("room %s created by %s: host port %d, guest ports %s", room.code, reply_to, room.host_port,
                 [s.port for s in room.slots])
        self._send_control(enc_created(room.code, room.host_token, room.host_port, [s.port for s in room.slots]), reply_to)

    def _handle_join(self, body: bytes, addr: Addr) -> None:
        code = body[:ROOM_CODE_LEN].decode("ascii", "replace").upper()
        (client_version,) = struct.unpack("<I", body[ROOM_CODE_LEN:ROOM_CODE_LEN + 4])
        room = self.rooms.get(code)
        if room is None or room.host_transport is None:
            self.stats["errors"] += 1
            self._send_control(enc_error(ERR_ROOM_NOT_FOUND, code), addr)
            return
        if not self._check_version(client_version, addr):
            return
        if room.client_version != client_version:
            self._send_control(enc_error(ERR_VERSION, "host runs a different DSCoop version"), addr)
            return
        # Retransmit tolerance: a guest re-sending JOIN from the same address gets its slot again.
        for s in room.slots:
            if s.assigned and getattr(s, "_joiner", None) == addr and time.monotonic() - s.assigned_at < 10:
                self._send_control(enc_joined(room.host_port, s.index, s.token), addr)
                return
        slot = room.free_slot()
        if slot is None:
            self.stats["errors"] += 1
            self._send_control(enc_error(ERR_ROOM_FULL), addr)
            return
        slot.token = secrets.randbits(64) or 1
        slot.assigned_at = time.monotonic()
        slot.guest_addr = None
        slot._joiner = addr  # type: ignore[attr-defined]
        self.stats["joins"] += 1
        log.info("room %s: %s joins as slot %d", room.code, addr, slot.index)
        self._send_control(enc_joined(room.host_port, slot.index, slot.token), addr)

    def _handle_close(self, body: bytes, addr: Addr) -> None:
        (token,) = struct.unpack("<Q", body[:8])
        room = self.rooms_by_token.get(token)
        if room is None:
            self._send_control(enc_error(ERR_BAD_TOKEN), addr)
            return
        self._close_room(room, f"closed by host {addr}")

    def _close_room(self, room: Room, why: str) -> None:
        if room.code not in self.rooms:
            return
        log.info("room %s closed (%s); forwarded host<-%d guests<-%d", room.code, why, room.forwarded_to_host,
                 room.forwarded_to_guests)
        del self.rooms[room.code]
        self.rooms_by_token.pop(room.host_token, None)
        if room.host_transport:
            room.host_transport.close()
        self.ports_free.append(room.host_port)
        for s in room.slots:
            if s.transport:
                s.transport.close()
            self.ports_free.append(s.port)
        self.ports_free.sort()

    # ------------------------------------------------------------------ data path
    def on_host_port(self, room: Room, data: bytes, addr: Addr) -> None:
        """Packet from a guest arriving at the room's host port."""
        self.stats["datagrams"] += 1
        if is_control(data):
            if data[5] == OP_PING and len(data) >= 6 + 9:
                token, slot_index = struct.unpack("<QB", data[6:15])
                for s in room.slots:
                    if s.index == slot_index and s.assigned and s.token == token:
                        if s.guest_addr != addr:
                            log.info("room %s slot %d guest mapping %s", room.code, s.index, addr)
                        s.guest_addr = addr
                        s.guest_last_seen = time.monotonic()
                        room.host_transport.sendto(enc_pong(s.index, now_ms()), addr)
                        return
                room.host_transport.sendto(enc_error(ERR_BAD_TOKEN), addr)
            return
        slot = room.slot_by_guest_addr(addr)
        if slot is None or slot.transport is None or slot.host_addr is None:
            return  # unknown guest or host mapping not learned yet: drop (ENet will retry)
        slot.guest_last_seen = time.monotonic()
        slot.transport.sendto(data, slot.host_addr)
        room.forwarded_to_host += 1

    def on_guest_port(self, room: Room, slot: Slot, data: bytes, addr: Addr) -> None:
        """Packet from the host arriving at one of the room's guest ports."""
        self.stats["datagrams"] += 1
        if is_control(data):
            if data[5] == OP_PING and len(data) >= 6 + 9:
                token, slot_index = struct.unpack("<QB", data[6:15])
                if token == room.host_token and slot_index == slot.index:
                    if slot.host_addr != addr:
                        log.info("room %s slot %d host mapping %s", room.code, slot.index, addr)
                    slot.host_addr = addr
                    slot.host_last_seen = time.monotonic()
                    room.last_host_activity = slot.host_last_seen
                    slot.transport.sendto(enc_pong(slot.index, now_ms()), addr)
                else:
                    slot.transport.sendto(enc_error(ERR_BAD_TOKEN), addr)
            return
        if slot.host_addr != addr or slot.guest_addr is None or room.host_transport is None:
            return
        room.last_host_activity = time.monotonic()
        room.host_transport.sendto(data, slot.guest_addr)
        room.forwarded_to_guests += 1


class _ControlProtocol(asyncio.DatagramProtocol):
    def __init__(self, relay: Relay):
        self.relay = relay

    def datagram_received(self, data: bytes, addr: Addr) -> None:
        self.relay.on_control(data, addr)

    def error_received(self, exc: Exception) -> None:
        log.debug("control socket error: %s", exc)


class _HostPortProtocol(asyncio.DatagramProtocol):
    def __init__(self, relay: Relay, room: Room):
        self.relay, self.room = relay, room

    def datagram_received(self, data: bytes, addr: Addr) -> None:
        self.relay.on_host_port(self.room, data, addr)

    def error_received(self, exc: Exception) -> None:
        pass  # ICMP unreachable from a vanished peer: ignore


class _GuestPortProtocol(asyncio.DatagramProtocol):
    def __init__(self, relay: Relay, room: Room, slot: Slot):
        self.relay, self.room, self.slot = relay, room, slot

    def datagram_received(self, data: bytes, addr: Addr) -> None:
        self.relay.on_guest_port(self.room, self.slot, data, addr)

    def error_received(self, exc: Exception) -> None:
        pass


def parse_range(s: str) -> Tuple[int, int]:
    a, b = s.split("-")
    lo, hi = int(a), int(b)
    if not (0 < lo <= hi < 65536):
        raise argparse.ArgumentTypeError("bad port range")
    return lo, hi


async def amain(args: argparse.Namespace) -> None:
    relay = Relay(args.control_port, args.port_range, args.room_idle, args.guest_idle, args.bind, args.require_version)
    await relay.start()
    stop = asyncio.Event()
    try:
        import signal
        for sig in (signal.SIGINT, signal.SIGTERM):
            asyncio.get_event_loop().add_signal_handler(sig, stop.set)
    except (NotImplementedError, ImportError):
        pass
    async def stats_task() -> None:
        while not stop.is_set():
            await asyncio.sleep(args.stats_interval)
            log.info("stats: rooms=%d %s", len(relay.rooms), relay.stats)
    if args.stats_interval > 0:
        asyncio.get_event_loop().create_task(stats_task())
    await stop.wait()
    await relay.stop()


def main() -> None:
    p = argparse.ArgumentParser(description="DSCoop relay / hub")
    p.add_argument("--bind", default="0.0.0.0")
    p.add_argument("--control-port", type=int, default=27000)
    p.add_argument("--port-range", type=parse_range, default=(27100, 27999), help="UDP ports for rooms, e.g. 27100-27999")
    p.add_argument("--room-idle", type=float, default=90.0, help="seconds without host pings before a room is closed")
    p.add_argument("--guest-idle", type=float, default=45.0, help="seconds without guest pings before a slot is freed")
    p.add_argument("--require-version", type=lambda s: int(s, 0), default=None, help="only accept this DSCoop client version")
    p.add_argument("--stats-interval", type=float, default=60.0)
    p.add_argument("--log-level", default="INFO")
    args = p.parse_args()
    logging.basicConfig(level=args.log_level.upper(), format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    asyncio.run(amain(args))


if __name__ == "__main__":
    main()
