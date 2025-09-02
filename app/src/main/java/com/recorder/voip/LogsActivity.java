package com.recorder.voip;

import android.os.Bundle;
import android.os.Handler;
import android.widget.ScrollView;
import android.widget.TextView;
import androidx.appcompat.app.AppCompatActivity;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;

public class LogsActivity extends AppCompatActivity {

    private TextView logTextView;
    private ScrollView scrollView;
    private boolean isRunning = true;
    private boolean cursorVisible = true;

    private final int MAX_LINES = 500; // keep last 500 lines
    private final Deque<String> logLines = new ArrayDeque<>();
    private final List<String> batchLogs = new ArrayList<>();
    private final Handler uiHandler = new Handler();
    private final int BATCH_DELAY_MS = 200; // update every 200ms

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_logs);

        logTextView = findViewById(R.id.logTextView);
        scrollView = findViewById(R.id.scrollView);

        startCursorBlink();
        startBatchUpdater();
        startLogcatStream();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        isRunning = false;
    }

    /** Start logcat stream */
    private void startLogcatStream() {
        new Thread(() -> {
            try {
                ProcessBuilder builder = new ProcessBuilder("su", "-c", "logcat -s AudioHook");
                builder.redirectErrorStream(true);
                Process process = builder.start();

                BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()));

                String line;
                while (isRunning && (line = reader.readLine()) != null) {
                    synchronized (batchLogs) {
                        batchLogs.add(line);
                    }
                }

                reader.close();
                process.destroy();
            } catch (Exception e) {
                appendLog("Error: " + e.getMessage());
            }
        }).start();
    }

    /** Collect logs and update UI in batches */
    private void startBatchUpdater() {
        uiHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (!isRunning) return;

                synchronized (batchLogs) {
                    for (String line : batchLogs) appendLog(line);
                    batchLogs.clear();
                }

                updateTextView();
                uiHandler.postDelayed(this, BATCH_DELAY_MS);
            }
        }, BATCH_DELAY_MS);
    }

    /** Keep only last MAX_LINES */
    private void appendLog(String logLine) {
        // Extract only the part after "AudioHook:"
        String filtered = logLine;
        int idx = logLine.indexOf("AudioHook:");
        if (idx != -1) {
            filtered = logLine.substring(idx);
        }

        if (logLines.size() >= MAX_LINES) {
            logLines.pollFirst(); // drop oldest
        }
        logLines.addLast(filtered);
    }

    /** Efficient text update */
    private void updateTextView() {
        StringBuilder sb = new StringBuilder();
        for (String l : logLines) {
            sb.append(l).append("\n");
        }
        if (cursorVisible) sb.append("|");
        logTextView.setText(sb.toString());

        scrollView.post(() -> scrollView.fullScroll(ScrollView.FOCUS_DOWN));
    }

    /** Cursor blinking */
    private void startCursorBlink() {
        uiHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (!isRunning) return;
                cursorVisible = !cursorVisible;
                updateTextView();
                uiHandler.postDelayed(this, 500);
            }
        }, 500);
    }
}
