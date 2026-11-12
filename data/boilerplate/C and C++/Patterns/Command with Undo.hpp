---
label: Command with Undo and Redo
order: 40
description: Actions as command objects that can undo themselves, kept in a history with undo and redo
includes: <memory> <utility> <vector>
---
// Every change goes through History::run(); each command knows how to reverse itself.
//     History history;
//     history.run(std::make_unique<SetValue>(volume, 11));
//     history.undo();
class Command
{
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
};

class History
{
public:
    void run(std::unique_ptr<Command> command)
    {
        command->execute();
        done_.push_back(std::move(command));
        undone_.clear(); // a new action ends what Redo could bring back
    }

    bool canUndo() const { return !done_.empty(); }
    bool canRedo() const { return !undone_.empty(); }

    void undo()
    {
        if (done_.empty())
        {
            return;
        }
        done_.back()->undo();
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
    }

    void redo()
    {
        if (undone_.empty())
        {
            return;
        }
        undone_.back()->execute();
        done_.push_back(std::move(undone_.back()));
        undone_.pop_back();
    }

private:
    std::vector<std::unique_ptr<Command>> done_;
    std::vector<std::unique_ptr<Command>> undone_;
};

// An example command: sets an int and remembers the old value.
class SetValue : public Command
{
public:
    SetValue(int& target, int value) : target_(target), value_(value) {}

    void execute() override { previous_ = std::exchange(target_, value_); }
    void undo() override { target_ = previous_; }

private:
    int& target_;
    int value_;
    int previous_ = 0;
};
