# Project

nsBLAST: Massively scalable authoritative DNS server written in C++.

Facts:
- Uses a gRPC stream for replication
- Provides a REST API with embedded swagger. You must always update the swagger definitions when changing the API.
- In this project perfomance and security matters a lot. Keep memory usage and CPU usage low. 
- The project is deployed in production
- Any changes that require schema changes or data migration must be implemented as incremental upgrades, so any new version of the server can upgrade from any previous version starting at v0.5.3.

# Rules and guidelines

Cache the files locally (not checked into git) and either refresh or check the timestamp on the HTTP server when you refresh your Agents instructions, or checkout the agents repository https://github.com/jgaa/agents.git outside the project and keep it up to date before you re-read the files.

https://raw.githubusercontent.com/jgaa/agents/refs/heads/main/rules/tlv-agents.md
https://raw.githubusercontent.com/jgaa/agents/refs/heads/main/rules/tlv-agents-cpp.md

