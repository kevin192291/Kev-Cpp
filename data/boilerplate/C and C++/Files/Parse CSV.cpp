---
order: 50
description: Reads a CSV file into rows of fields, with quoted fields, commas inside quotes and "" escapes
includes: <filesystem> <fstream> <stdexcept> <string> <string_view> <utility> <vector>
---
// parseCsvLine(R"(1,"Smith, Jo","say ""hi""")") gives {"1", "Smith, Jo", "say \"hi\""}.
// A quoted field can't span lines here; files that need that want a full CSV library.
std::vector<std::string> parseCsvLine(std::string_view line, char separator = ',')
{
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (inQuotes)
        {
            if (c != '"')
            {
                field += c;
            }
            else if (i + 1 < line.size() && line[i + 1] == '"')
            {
                field += '"'; // "" inside quotes is one quote
                ++i;
            }
            else
            {
                inQuotes = false;
            }
        }
        else if (c == '"')
        {
            inQuotes = true;
        }
        else if (c == separator)
        {
            fields.push_back(std::move(field));
            field.clear();
        }
        else
        {
            field += c;
        }
    }
    fields.push_back(std::move(field));
    return fields;
}

// Every non-empty line of the file; rows[0] is the header when the file has one.
std::vector<std::vector<std::string>> readCsv(const std::filesystem::path& path, char separator = ',')
{
    std::ifstream in(path);
    if (!in)
    {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::vector<std::vector<std::string>> rows;
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            rows.push_back(parseCsvLine(line, separator));
        }
    }
    return rows;
}
