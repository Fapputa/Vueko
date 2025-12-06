import sys
import os
from playwright.sync_api import sync_playwright
import shutil
from urllib.parse import urljoin
import filetype 

URL = sys.argv[1]

BLOCKED_RESOURCES = ['image', 'media', 'font', 'stylesheet', 'script']
BLOCKED_URLS = ['doubleclick.net', 'google-analytics.com', 'quantserve.com', 'wikia-services.com/api/ad-info']
IMAGE_DIR = "datas/images"

def block_ads_and_trackers(route):
    for url_part in BLOCKED_URLS:
        if url_part in route.request.url:
            route.abort()
            return
    
    if route.request.resource_type in BLOCKED_RESOURCES:
        route.abort()
    else:
        route.continue_()

def get_image_extension(content_type, data=None):
    if data:
        kind = filetype.guess(data)
        if kind:
            return "." + kind.extension
    
    content_type = content_type.lower()
    if 'image/jpeg' in content_type or 'image/jpg' in content_type:
        return ".jpg"
    elif 'image/png' in content_type:
        return ".png"
    elif 'image/gif' in content_type:
        return ".gif"
    elif 'image/webp' in content_type:
        return ".webp"
    elif 'image/svg+xml' in content_type:
        return ".svg"
    else:
        return ".bin"

# --- Extracting URLs
image_urls = []
link_urls = []

with sync_playwright() as p:
    timeout_ms = 30000 
    
    browser = p.chromium.launch(headless=True)
    page = browser.new_page(
        user_agent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                   "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        locale="fr-FR",
    )
    
    page.route("**/*", block_ads_and_trackers)

    try:
        page.goto(URL, wait_until=None, timeout=timeout_ms)
        page.wait_for_timeout(5000)
        
        with open("temp.html","w") as f:
            f.write(page.content())
            
    except Exception as e:
        pass

    links_elements = page.query_selector_all("a")
    images_elements = page.query_selector_all("img")
    
    for image in images_elements:
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
            
        image_urls.append(link)
        
    for a in links_elements:
        href = a.get_attribute("href")
        if not href:
            continue

        link = urljoin(URL, href)
        link_urls.append(link)
    
    browser.close()

# --- Downloading imgs

if os.path.exists(IMAGE_DIR):
    shutil.rmtree(IMAGE_DIR)
os.makedirs(IMAGE_DIR, exist_ok=True)

nimg = 0

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    
    user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
    
    full_headers = {
        "Referer": URL,
        "Accept": "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8",
        "Accept-Encoding": "gzip, deflate, br",
        "Accept-Language": "fr-FR,fr;q=0.9,en-US;q=0.8,en;q=0.7",
        "User-Agent": user_agent
    }
    
    context = browser.new_context(
        user_agent=user_agent, 
        extra_http_headers=full_headers 
    )
    page = context.new_page() 
    
    for link in image_urls:
        data = None
        content_type = ""
        
        try:
            response = page.goto(link, timeout=10000) 
            
            if response.status >= 400:
                continue
            
            data = response.body()
            content_type = response.headers.get('content-type', '')
            
        except Exception as e:
            continue 
            
        if not data:
            continue
            
        ext = get_image_extension(content_type, data)

        filename = os.path.join(IMAGE_DIR, f"img{nimg}{ext}")
        with open(filename, "wb") as f:
            f.write(data)

        nimg += 1
        
    page.close() 
    browser.close() 

# --- saving links

with open("templink.csv","w") as f:
    pass 

for link in link_urls:
    with open("templink.csv","a") as f:
        f.write(link + "\n")