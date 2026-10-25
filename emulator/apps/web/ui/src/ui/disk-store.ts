/**
 * The last disk in each drive, kept in this browser for next time. Disks
 * are too big for localStorage (a WOZ is about 230 KB), so they go in
 * IndexedDB: database "oo1", store "disks", keyed by drive. Like the ROMs,
 * they stay here and are never uploaded.
 *
 * Every call settles, never throws: private windows and blocked storage
 * just keep nothing.
 */

import type { DiskFormat } from "../port/machine-port";

export interface KeptDisk {
  name: string;
  bytes: Uint8Array;
  writeProtected: boolean;
  /** The format it came in, which changes are kept in while they still fit it. */
  format: DiskFormat;
}

export interface DiskStore {
  get(drive: number): Promise<KeptDisk | null>;
  /** @returns false if the browser would not keep it */
  put(drive: number, disk: KeptDisk): Promise<boolean>;
  remove(drive: number): Promise<void>;
}

/** For tests, and wherever IndexedDB is missing. */
export function memoryDiskStore(): DiskStore {
  const kept = new Map<number, KeptDisk>();
  return {
    get: async (drive) => kept.get(drive) ?? null,
    put: async (drive, disk) => (kept.set(drive, disk), true),
    remove: async (drive) => void kept.delete(drive),
  };
}

export function browserDiskStore(): DiskStore {
  if (typeof indexedDB === "undefined") return memoryDiskStore();
  let db: Promise<IDBDatabase | null> | null = null;
  const open = () =>
    (db ??= new Promise((resolve) => {
      try {
        const req = indexedDB.open("oo1", 1);
        req.onupgradeneeded = () => req.result.createObjectStore("disks");
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => resolve(null);
        req.onblocked = () => resolve(null);
      } catch {
        resolve(null);
      }
    }));
  const run = async <T>(mode: IDBTransactionMode, op: (s: IDBObjectStore) => IDBRequest, fallback: T, done: (r: unknown) => T) => {
    const d = await open();
    if (!d) return fallback;
    return new Promise<T>((resolve) => {
      try {
        const req = op(d.transaction("disks", mode).objectStore("disks"));
        req.onsuccess = () => resolve(done(req.result));
        req.onerror = () => resolve(fallback);
      } catch {
        resolve(fallback);
      }
    });
  };
  return {
    get: (drive) => run("readonly", (s) => s.get(drive), null, (r) => (r as KeptDisk | undefined) ?? null),
    put: (drive, disk) => run("readwrite", (s) => s.put(disk, drive), false, () => true),
    remove: (drive) => run("readwrite", (s) => s.delete(drive), undefined, () => undefined),
  };
}
