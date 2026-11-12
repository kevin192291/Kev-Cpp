---
label: Wrap in try/catch
order: 10
description: Wraps the selection in try/catch that prints any std::exception's message
includes: <iostream>
---
try
{
    {{selection}}{{cursor}}
}
catch (const std::exception& e)
{
    std::cerr << "error: " << e.what() << '\n';
}
