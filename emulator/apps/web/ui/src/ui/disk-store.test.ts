import { afterEach, describe, expect, test, vi } from "vitest";
import { browserDiskStore, type KeptDisk } from "./disk-store";

const DISK: KeptDisk = { name: "a.dsk", bytes: new Uint8Array([1]), writeProtected: false, format: "dsk" };

/** An IndexedDB whose open succeeds only when told to. */
function slowIdb() {
  const rows = new Map<IDBValidKey, unknown>();
  const answer = (value: () => unknown) => {
    const req = {} as { result?: unknown; onsuccess?: () => void };
    queueMicrotask(() => ((req.result = value()), req.onsuccess?.()));
    return req;
  };
  const store = {
    get: (k: IDBValidKey) => answer(() => rows.get(k)),
    put: (v: unknown, k: IDBValidKey) => answer(() => rows.set(k, v)),
    delete: (k: IDBValidKey) => answer(() => rows.delete(k)),
  };
  const db = { transaction: () => ({ objectStore: () => store }) };
  const req = {} as { result?: unknown; onsuccess?: () => void };
  return {
    rows,
    opens: 0,
    open() {
      ++this.opens;
      return req;
    },
    succeed: () => ((req.result = db), req.onsuccess?.()),
  };
}

afterEach(() => void vi.useRealTimers());

describe("browserDiskStore", () => {
  test("an open that hangs: nothing kept after 5 s; once it opens, it is used", async () => {
    vi.useFakeTimers();
    const idb = slowIdb();
    const store = browserDiskStore(idb as unknown as IDBFactory);
    const put = store.put(0, DISK);
    await vi.advanceTimersByTimeAsync(5000);
    expect(await put).toBe(false);
    idb.succeed(); // late
    expect(await store.put(0, DISK)).toBe(true);
    expect((await store.get(0))?.name).toBe("a.dsk");
    expect(idb.opens).toBe(1);
  });

  test("an open in time clears its timer", async () => {
    vi.useFakeTimers();
    const idb = slowIdb();
    const store = browserDiskStore(idb as unknown as IDBFactory);
    const put = store.put(0, DISK);
    idb.succeed();
    expect(await put).toBe(true);
    expect(vi.getTimerCount()).toBe(0);
  });
});
