---
description: A badge showing whether the GitHub Actions workflow passes, linked to its runs
ask: Repository = owner/repo
ask: Workflow = build.yml
order: 23
section: parts
---
[![Build](https://github.com/{{Repository}}/actions/workflows/{{Workflow}}/badge.svg)](https://github.com/{{Repository}}/actions/workflows/{{Workflow}})
