            end

            if (decode_start) begin
                normal_fifo_full_cycles <= 0;
                normal_fifo_empty_cycles <= 0;
                s45_fifo_full_cycles <= 0;
                s45_fifo_empty_cycles <= 0;
            end else begin
                // Count queue pressure only during the work-stealing phase (D_RUN=3).
                if (normal.core.decode_busy && normal.core.d_state == 3) begin
                    if ((normal.core.task_count - normal.core.task_head) == 2048)
                        normal_fifo_full_cycles <= normal_fifo_full_cycles + 1;
                    if (normal.core.task_count == normal.core.task_head)
                        normal_fifo_empty_cycles <= normal_fifo_empty_cycles + 1;
                end
                if (compact.core.decode_busy && compact.core.d_state == 3) begin
                    if ((compact.core.task_count - compact.core.task_head) == 2048)
                        s45_fifo_full_cycles <= s45_fifo_full_cycles + 1;
                    if (compact.core.task_count == compact.core.task_head)
                        s45_fifo_empty_cycles <= s45_fifo_empty_cycles + 1;
                end
            end
        end
    end
endmodule
