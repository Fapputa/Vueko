import sys
import os
from playwright.sync_api import sync_playwright
import requests
import shutil
import filetype
from urllib.parse import urljoin
import io

URL = sys.argv[1]

# Liste des types de ressources à bloquer, y compris 'script'
BLOCKED_RESOURCES = ['image', 'media', 'font', 'stylesheet','script']

# Liste d'URL de publicités/trackers connus à bloquer spécifiquement
BLOCKED_URLS = ['doubleclick.net', 'google-analytics.com', 'quantserve.com', 'wikia-services.com/api/ad-info'] 

def block_ads_and_trackers(route):
    """Bloque les requêtes pour les types de ressources et les domaines indésirables."""
    for url_part in BLOCKED_URLS:
        if url_part in route.request.url:
            route.abort()
            return
    
    # Bloquer les ressources définies (y compris les scripts)
    if route.request.resource_type in BLOCKED_RESOURCES:
        route.abort()
    else:
        route.continue_()

with sync_playwright() as p:
    # Réduction du timeout de navigation, car nous n'attendons plus une condition stricte
    timeout_ms = 30000 
    
    browser = p.chromium.launch(headless=True)
    page = browser.new_page(
        user_agent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        locale="fr-FR",
    )
    
    # Configuration du routage pour bloquer les requêtes
    page.route("**/*", block_ads_and_trackers)

    try:
        # STRATÉGIE AGRESSIVE : wait_until=None (n'attendre rien)
        page.goto(URL, wait_until=None, timeout=timeout_ms)
        
        # Pause fixe réduite à 5 secondes car sans JS le chargement est très rapide
        page.wait_for_timeout(5000)
        
        with open("datas/cache/temp.html","w") as f:
            f.write(page.content())
            
    except Exception as e:
        print("Impossible d'acceder a la page :", e)

    links = page.locator("a").all()
    images = page.locator("img").all()

    if os.path.exists("datas/images"):
        shutil.rmtree("datas/images")
    os.mkdir("datas/images")

    nimg = 0

    for image in images:
        src = image.get_attribute("src")
        if not src:
            srcset = image.get_attribute("srcset")
            if srcset:
                src = srcset.split(',')[0].strip().split(' ')[0]
        
        if not src:
            continue
            
        link = urljoin(URL, src)
        if link.startswith("data:") or len(link) < 5:
            continue
        
        data = None
        r = None 
        try:
            r = requests.get(link, timeout=10)
            r.raise_for_status()
            data = r.content
        except requests.exceptions.RequestException as e:
            continue
        except Exception as e:
            continue
        
        if not data:
            continue
        
        kind = filetype.guess(data) 
        
        if kind:
            ext = "." + kind.extension
        else:
            if r and r.headers:
                content_type = r.headers.get('Content-Type', '').lower()
                if 'image/jpeg' in content_type or 'image/jpg' in content_type:
                    ext = ".jpg"
                elif 'image/png' in content_type:
                    ext = ".png"
                elif 'image/gif' in content_type:
                    ext = ".gif"
                else:
                    ext = ".bin"
            else:
                 ext = ".bin" 

        filename = f"datas/images/img{nimg}{ext}"
        with open(filename, "wb") as f:
            f.write(data)

        nimg += 1

    with open("datas/cache/templink.csv","w") as f:
        pass 

    for a in links:
        href = a.get_attribute("href")
        if not href:
            continue

        link = urljoin(URL, href)

        with open("datas/cache/templink.csv","a") as f:
            f.write(link + "\n")

    browser.close()
