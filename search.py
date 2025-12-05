import sys
import os
from playwright.sync_api import sync_playwright
import requests
import shutil
import filetype
from urllib.parse import urljoin
import io

RESEARCH = sys.argv[1]
URL = "https://www.bing.com/search?pc=MOZI&form=MOZLBR&q=" + RESEARCH

with sync_playwright() as p:
    timeout_ms = 30000 
    
    browser = p.chromium.launch(headless=True)
    page = browser.new_page(
        user_agent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        locale="fr-FR",
    )
    
    try:
        page.goto(URL, wait_until=None, timeout=timeout_ms)
        
        page.wait_for_timeout(5000)
        
        with open("datas/cache/search_results.html","w") as f:
            f.write(page.content())
            
    except Exception as e:
        print("Impossible d'acceder a la page :", e)

    data = []

    results = page.locator("#b_results li.b_algo")
    count = results.count()

    for i in range(count):
        result = results.nth(i)

        title = result.locator("h2 a").inner_text()
        url = result.locator("h2 a").get_attribute("href")

        try:
            site = result.locator(".b_attribution cite").inner_text()
        except:
            site = None

        if not site and url:
            site = urlparse(url).hostname

        data.append({
            "title": title,
            "url": url,
            "site": site
        })

    links = page.locator("a").all()

    with open("datas/cache/search_results.csv","w") as f:
        pass 

    for di in data:
        with open("datas/cache/search_results.csv","a") as f:
            f.write(di["title"]+";"+di["url"]+";"+di["site"]+"\n")

    if os.path.exists("datas/cache/search_results.html"):
        os.remove("datas/cache/search_results.html")

    browser.close()
