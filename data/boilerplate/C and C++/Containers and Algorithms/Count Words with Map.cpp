---
order: 60
description: Counts each word of a text in a std::map, ignoring case and punctuation, and lists the most frequent
includes: <algorithm> <cctype> <functional> <iostream> <map> <sstream> <string> <utility> <vector>
---
std::map<std::string, int> counts;
std::istringstream words(text);
for (std::string word; words >> word;)
{
    // Lower case and no punctuation, so "The" and "the," count as the same word.
    std::erase_if(word, [](unsigned char c) { return std::ispunct(c); });
    std::ranges::transform(word, word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!word.empty())
    {
        ++counts[word]; // a new word starts at 0
    }
}

// The map is alphabetical; copy it out to list the most frequent first.
std::vector<std::pair<std::string, int>> byCount(counts.begin(), counts.end());
std::ranges::stable_sort(byCount, std::ranges::greater{}, &std::pair<std::string, int>::second);
for (const auto& [word, count] : byCount)
{
    std::cout << word << ": " << count << '\n';
}
