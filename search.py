import sys
import os
import json
from urllib.parse import urlparse, unquote, parse_qs, quote_plus
from playwright.sync_api import sync_playwright, TimeoutError as PlaywrightTimeoutError
import base64

# Joindre tous les arguments avec des espaces (gère "programmation c" passé en un ou plusieurs args)
research = " ".join(sys.argv[1:]).strip()
if not research:
    sys.exit(1)

# Encoder proprement pour l'URL Bing
URL = "https://www.bing.com/search?pc=MOZI&form=MOZLBR&q=" + quote_plus(research)

output_file = "datas/cache/search_results.json"
os.makedirs(os.path.dirname(output_file), exist_ok=True)

def extract_real_url(bing_url: str) -> str:
    """Extrait l'URL réelle depuis une URL de redirection Bing."""
    if not bing_url:
        return bing_url
    if "bing.com/ck/a" in bing_url:
        try:
            parts = parse_qs(bing_url.split("?", 1)[-1])
            u = parts.get("u", [None])[0]
            if u and u.startswith("a1"):
                b64 = u[2:]
                b64 += "=" * (4 - len(b64) % 4)
                decoded = base64.urlsafe_b64decode(b64).decode("utf-8", errors="replace")
                if decoded.startswith("http"):
                    return decoded
        except Exception:
            pass
    return bing_url

with sync_playwright() as p:
    timeout_ms = 30000
    browser = p.chromium.launch(headless=True)
    page = browser.new_page(
        user_agent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                   "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        locale="fr-FR",
    )

    try:
        page.goto(URL, wait_until="domcontentloaded", timeout=timeout_ms)
        page.wait_for_timeout(2000)
    except PlaywrightTimeoutError:
        browser.close()
        sys.exit(1)

    results = page.locator("#b_results li.b_algo")
    count = results.count()
    data = []

    for i in range(count):
        result = results.nth(i)
        try:
            title    = result.locator("h2 a").inner_text()
            bing_url = result.locator("h2 a").get_attribute("href")
        except:
            continue

        final_url = extract_real_url(bing_url)

        try:
            site = result.locator(".b_attribution cite").inner_text()
        except:
            site = None

        if not site and final_url:
            site = urlparse(final_url).hostname

        if "bing.com/ck/a" in final_url:
            continue
        data.append({
            "title": title,
            "url":   final_url,
            "site":  site
        })

    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=4)

    browser.close()