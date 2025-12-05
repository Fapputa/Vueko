import sys
import os
import json
from urllib.parse import urlparse
from playwright.sync_api import sync_playwright, TimeoutError as PlaywrightTimeoutError
import requests

# Préparation de la recherche
research = sys.argv[1:]
research = "+".join(research)
URL = "https://www.bing.com/search?pc=MOZI&form=MOZLBR&q=" + research

output_file = "datas/cache/search_results.json"
os.makedirs(os.path.dirname(output_file), exist_ok=True)

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
        page.wait_for_timeout(3000)  # Temps pour que JS charge éventuellement
    except PlaywrightTimeoutError:
        print("Impossible d'accéder à la page.")
        browser.close()
        sys.exit(1)

    results = page.locator("#b_results li.b_algo")
    count = results.count()
    data = []

    for i in range(count):
        result = results.nth(i)
        try:
            title = result.locator("h2 a").inner_text()
            bing_url = result.locator("h2 a").get_attribute("href")
        except:
            continue

        # Suivi du lien Bing pour récupérer l'URL finale
        final_url = None
        try:
            response = requests.head(bing_url, allow_redirects=True, timeout=10)
            final_url = response.url
        except requests.RequestException:
            final_url = bing_url  # fallback si problème

        try:
            site = result.locator(".b_attribution cite").inner_text()
        except:
            site = None

        if not site and final_url:
            site = urlparse(final_url).hostname

        data.append({
            "title": title,
            "url": final_url,
            "site": site
        })

    # Sauvegarde JSON
    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=4)

    browser.close()

print(f"Résultats sauvegardés dans {output_file}")
