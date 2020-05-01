#pragma once

#include <algorithm>

namespace
{
template <typename vec_t>
bool push_back_unique(vec_t& vector, const typename vec_t::value_type& value)
{
    if(std::none_of(begin(vector), end(vector), [&value](const typename vec_t::value_type& existing_value) {
           return existing_value == value;
       }))
    {
        vector.push_back(value);
        return true;
    }
    return false;
}
} // namespace


template <typename string_t, typename ret_t, typename arg_t> struct Chatter
{
    struct Command
    {
        string_t                    cmd_text;
        string_t                    help_text;
        std::function<ret_t(arg_t)> callback;

        Command(string_t cmd, string_t h_txt, std::function<ret_t(arg_t)> callb)
        : cmd_text(std::move(cmd)), help_text(std::move(h_txt)), callback(std::move(callb))
        {
        }

        Command(string_t cmd, std::function<ret_t(arg_t)> callb) : Command(cmd, string_t(), callb)
        {
        }

        bool operator==(const Command& rhs) const { return cmd_text == rhs.cmd_text; }

        bool operator!=(const Command& rhs) const { return !operator==(rhs); }
    };

    std::vector<Command> commands;

    bool add(string_t cmd_text, std::function<ret_t(arg_t)> callback)
    {
        return push_back_unique(commands, Command(cmd_text, string_t{}, callback));
    }

    bool add(string_t cmd_text, string_t help_text, std::function<ret_t(arg_t)> callback)
    {
        return push_back_unique(commands, Command(cmd_text, help_text, callback));
    }

    ret_t handle_incomoing_message(const string_t& msg, arg_t arg)
    {
        auto it = std::find_if(commands.begin(), commands.end(), [&msg](const Command& cmd) {
            return std::equal(cmd.cmd_text.begin(), cmd.cmd_text.end(), msg.begin());
        });
        if(it != commands.end())
        {
            return it->callback(arg);
        }
        return ret_t();
    }
};