import { describe, expect, test } from "vitest";
import { chromium } from "./browser";

const CHROME_MAC = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";
const EDGE_WIN = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36 Edg/130.0.0.0";
const SAFARI_MAC = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15";
const FIREFOX = "Mozilla/5.0 (X11; Linux x86_64; rv:131.0) Gecko/20100101 Firefox/131.0";
const CHROME_IOS = "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) CriOS/130.0.0.0 Mobile/15E148 Safari/604.1";
const EDGE_IOS = "Mozilla/5.0 (iPad; CPU OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) EdgiOS/130.0.0.0 Mobile/15E148 Safari/605.1.15";

describe("chromium", () => {
  test("Chromium by its brand, whatever the user agent says", () => {
    expect(chromium({ userAgent: "", userAgentData: { brands: [{ brand: "Not A;Brand" }, { brand: "Chromium" }] } })).toBe(true);
    expect(chromium({ userAgent: SAFARI_MAC, userAgentData: { brands: [{ brand: "Not A;Brand" }] } })).toBe(false);
  });

  test("by its user agent without userAgentData: Chrome and Edge are, Safari and Firefox aren't", () => {
    expect(chromium({ userAgent: CHROME_MAC })).toBe(true);
    expect(chromium({ userAgent: EDGE_WIN })).toBe(true);
    expect(chromium({ userAgent: SAFARI_MAC })).toBe(false);
    expect(chromium({ userAgent: FIREFOX })).toBe(false);
  });

  test("iOS's Chrome and Edge are WebKit underneath, so not Chromium", () => {
    expect(chromium({ userAgent: CHROME_IOS })).toBe(false);
    expect(chromium({ userAgent: EDGE_IOS })).toBe(false);
  });
});
