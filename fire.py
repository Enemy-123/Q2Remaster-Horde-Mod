import os

# Keywords and filters
KEYWORD = "fire_"
EXCLUDED_FILES = {"m_turret.cpp", "m_move.cpp", "m_insane.cpp", "m_actor.cpp"}
OUTPUT_FILE = "fire_results.txt"

def search_files(root="."):
    results = []

    for dirpath, _, filenames in os.walk(root):
        for filename in filenames:
            if filename.startswith("m_") and filename.endswith(".cpp") and filename not in EXCLUDED_FILES:
                filepath = os.path.join(dirpath, filename)

                try:
                    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                        for i, line in enumerate(f, start=1):
                            if KEYWORD in line:
                                result = f"{filepath}:{i}: {line.strip()}"
                                results.append(result)
                except Exception as e:
                    print(f"Could not read {filepath}: {e}")

    return results


def main():
    results = search_files(".")
    if results:
        print(f"Found {len(results)} matches for '{KEYWORD}'")
        for r in results:
            print(r)

        with open(OUTPUT_FILE, "w", encoding="utf-8") as out:
            out.write("\n".join(results))

        print(f"\nResults saved to {OUTPUT_FILE}")
    else:
        print(f"No matches found for '{KEYWORD}'")


if __name__ == "__main__":
    main()
