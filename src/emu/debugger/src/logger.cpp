#include <debugger/logger.h>

namespace eka2l1 {
    void imgui_logger::clear() {
        buf.clear();
        line_offsets.clear();
    }

    void imgui_logger::log(const char *fmt, ...) {
        int old_size = buf.size();
        va_list args;
        va_start(args, fmt);

        {
            const std::lock_guard<std::mutex> guard(log_lock);
            buf.appendfv(fmt, args);
        }

        va_end(args);

        {
            const std::lock_guard<std::mutex> guard(log_lock);

            for (int new_size = buf.size(); old_size < new_size; old_size++) {
                if (buf[old_size] == '\n') {
                    line_offsets.push_back(old_size);
                }
            }
        }

        scroll_to_bottom = true;
    }

    /* Data not change but only be added, corruption is small chance */
    void imgui_logger::draw(const char *title, bool *p_opened) {
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiSetCond_FirstUseEver);
        ImGui::Begin(title, p_opened);

        if (ImGui::Button("Clear")) {
            clear();
        }

        ImGui::SameLine();
        bool copy = ImGui::Button("Copy");
        ImGui::SameLine();
        filter.Draw("Filter", -100.0f);
        ImGui::Separator();
        ImGui::BeginChild("scrolling");
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));

        if (copy) {
            ImGui::LogToClipboard();
        }

        {
            const std::lock_guard<std::mutex> guard(log_lock);

            if (filter.IsActive()) {
                const char *buf_begin = buf.begin();
                const char *line = buf_begin;

                for (int line_no = 0; line != nullptr; line_no++) {
                    const char *line_end = (line_no < line_offsets.Size) ? buf_begin + line_offsets[line_no] : NULL;

                    if (filter.PassFilter(line, line_end)) {
                        ImGui::TextUnformatted(line, line_end);
                    }

                    line = line_end && line_end[1] ? line_end + 1 : nullptr;
                }
            } else {
                ImGui::TextUnformatted(buf.begin(), buf.end());
            }
        }

        if (scroll_to_bottom) {
            ImGui::SetScrollHere(1.0f);
        }

        scroll_to_bottom = false;
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::End();
    }
}