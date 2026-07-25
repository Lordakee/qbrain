# N2 Plan — 页面/图谱契约

**Status**: draft → implement after N1  
**Depends on**: N1  

## Goal

- `get_backlinks` op  
- `delete_page` / soft-delete already CLI; add MCP op  
- `purge_deleted` localOnly  
- page_versions table (basic)  
- link_type richer defaults  

## Ledger

get_backlinks, delete_page, purge_deleted_pages → implemented  

## Acceptance

- backlinks query returns inbound edges  
- purge removes soft-deleted older than N hours  
