"""Atomic host state with one local owner; no broker secrets are stored here."""
import json
from pathlib import Path
import sqlite3

from .client import unique

APPLICATION_ID = 0x55544854


class Journal:
    """Hold SQLite's exclusive file lock across commits until close.

    EXTRA synchronous commits finish before a command can be published. A crash
    after publication therefore leaves its exact packet available for recovery.
    Use a local filesystem and one journal per node, never a shared/network file.
    """

    def __init__(self, path):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(path, timeout=0, isolation_level=None)
        try:
            app_id = self.db.execute('PRAGMA application_id').fetchone()[0]
            tables = self.db.execute("SELECT name FROM sqlite_master WHERE type='table'").fetchall()
            if app_id not in (0, APPLICATION_ID) or (app_id == 0 and tables):
                raise ValueError('not a transaction controller journal')
            self.db.execute('PRAGMA locking_mode=EXCLUSIVE')
            self.db.execute('PRAGMA journal_mode=DELETE')
            self.db.execute('PRAGMA synchronous=EXTRA')
            self.db.execute('BEGIN EXCLUSIVE')
            self.db.execute(f'PRAGMA application_id={APPLICATION_ID}')
            self.db.execute('CREATE TABLE IF NOT EXISTS controller (id INTEGER PRIMARY KEY CHECK(id=1), state TEXT NOT NULL)')
            self.db.execute('COMMIT')
        except Exception:
            self.db.close()
            raise

    def read(self):
        row = self.db.execute('SELECT state FROM controller WHERE id=1').fetchone()
        return None if row is None else json.loads(row[0], object_pairs_hook=unique)

    def write(self, state):
        value = json.dumps(state, allow_nan=False, separators=(',', ':'))
        self.db.execute('BEGIN EXCLUSIVE')
        try:
            self.db.execute('INSERT OR REPLACE INTO controller VALUES (1, ?)', (value,))
            self.db.execute('COMMIT')
        except Exception:
            if self.db.in_transaction:
                self.db.execute('ROLLBACK')
            raise

    def close(self):
        self.db.close()
