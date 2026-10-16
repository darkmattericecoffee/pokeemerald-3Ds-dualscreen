"""The live channel: every upload, vote, pin, delete and console link, for the web app's
Server-Sent Events stream (/api/web/events).

In memory only. Each event gets a rising id. Every stream starts with a "hello"; a browser
that had to reconnect fetches everything again on it, so nothing missed in between is lost.
Standard library only.
"""
import collections
import threading
import time
from typing import List, Tuple

KEEP = 200          # recent events (a stream reads the ones after its last)
MAX_STREAMS = 64    # open streams at once (each holds a server thread)


class Bus:
    def __init__(self):
        self.cond = threading.Condition()
        self.events: "collections.deque[Tuple[int, dict]]" = collections.deque(maxlen=KEEP)
        self.last_id = 0
        self.streams = 0

    def publish(self, kind: str, **data) -> int:
        with self.cond:
            self.last_id += 1
            event = dict(data, type=kind, at=int(time.time()))
            self.events.append((self.last_id, event))
            self.cond.notify_all()
            return self.last_id

    def wait(self, last: int, timeout: float) -> List[Tuple[int, dict]]:
        """Events after `last`, waiting up to `timeout` seconds for the first."""
        with self.cond:
            if self.last_id <= last:
                self.cond.wait(timeout)
            return [(i, e) for i, e in self.events if i > last]

    def open_stream(self) -> bool:
        with self.cond:
            if self.streams >= MAX_STREAMS:
                return False
            self.streams += 1
            return True

    def close_stream(self) -> None:
        with self.cond:
            self.streams -= 1


BUS = Bus()

