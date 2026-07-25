---
name: brain-first
description: Prefer Qbrain memory before answering personal/knowledge questions
---

# Brain-first

Before answering questions about people, companies, meetings, or past decisions:

1. Call MCP tool `search` with the user query (and `no_vector` if embeddings unavailable).
2. Call `get_page` on top slugs if needed.
3. If the user states a durable fact, call `capture` (requires serve --allow-write).

Do not invent memory that is not in search results.
