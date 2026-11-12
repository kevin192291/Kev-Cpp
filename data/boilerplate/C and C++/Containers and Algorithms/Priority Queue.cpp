---
order: 100
description: A std::priority_queue with a custom order that always hands out the most urgent task first
includes: <queue> <string> <vector>
---
// top() is the element the comparison ranks highest, here the largest priority. For plain numbers:
// std::priority_queue<int> gives the largest first, std::priority_queue<int, std::vector<int>, std::greater<>>
// the smallest first.
auto lessUrgent = [](const Task& a, const Task& b) { return a.priority < b.priority; };
std::priority_queue<Task, std::vector<Task>, decltype(lessUrgent)> tasks(lessUrgent);

tasks.push(Task{"write the report", 2});
tasks.push(Task{"fix the build", 5});
while (!tasks.empty())
{
    const Task task = tasks.top();
    tasks.pop();
    {{cursor}}
}
