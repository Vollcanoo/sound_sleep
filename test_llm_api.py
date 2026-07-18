import json
import requests

# ============================================================
#  Config - keep in sync with cloud_llm_client.h
# ============================================================
API_URL = "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
API_KEY = "your-api-key-here"  # Replace with your API Key
MODEL   = "deepseek-v4-pro-260425"

def main():
    print("+" + "=" * 42 + "+")
    print("|  VolcEngine LLM API Connectivity Test     |")
    print("+" + "=" * 42 + "+")
    print(f"  API URL: {API_URL}")
    print(f"  Model:   {MODEL}")
    print()

    request_body = {
        "model": MODEL,
        "max_tokens": 100,
        "messages": [
            {"role": "user", "content": "Hello"}
        ]
    }

    headers = {
        "Content-Type": "application/json",
        "Authorization": f"Bearer {API_KEY}",
    }

    print("Sending request...")
    try:
        resp = requests.post(API_URL, headers=headers, json=request_body, timeout=60)
        print(f"HTTP Status: {resp.status_code}")

        if resp.status_code == 200:
            data = resp.json()
            content = data["choices"][0]["message"]["content"]
            print(f"LLM Response: {content}")

            usage = data.get("usage", {})
            if usage:
                print(f"Token Usage: prompt={usage.get('prompt_tokens')} "
                      f"completion={usage.get('completion_tokens')} "
                      f"total={usage.get('total_tokens')}")
        else:
            print(f"API Error!")
            print(f"Response: {resp.text[:500]}")

    except requests.exceptions.ConnectionError:
        print("Connection failed - check network or API URL")
    except requests.exceptions.Timeout:
        print("Request timed out (60s)")
    except Exception as e:
        print(f"Exception: {e}")

if __name__ == "__main__":
    main()
