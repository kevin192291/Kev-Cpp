---
label: Wrap in try/catch
order: 90
section: errors
description: Wraps the selected code in try/catch
---
try {
    {{selection}}
} catch (error) {
    console.error(error);{{cursor}}
}
