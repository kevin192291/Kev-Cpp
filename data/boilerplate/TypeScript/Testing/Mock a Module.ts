---
order: 30
description: vi.mock: replaces one export of a module the code under test imports, typed through vi.mocked
ask: Module = ./api
ask: Function = fetchUser
---
// vi.mock calls are moved above the imports, so the code under test gets the mock too.
// The path is relative to this test file. The module's other exports stay real.
vi.mock('{{Module}}', async (importOriginal) => ({
    ...(await importOriginal<typeof import('{{Module}}')>()),
    {{Function}}: vi.fn(),
}));
import { {{Function}} } from '{{Module}}';

// In a test, choose what it returns, then check how it was called:
//     vi.mocked({{Function}}).mockResolvedValue({ id: 1, name: 'Ada' });
//     expect({{Function}}).toHaveBeenCalledWith(1);
// Add beforeEach(() => vi.mocked({{Function}}).mockReset()) so each test starts fresh.
