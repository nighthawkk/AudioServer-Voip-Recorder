package com.recorder.voip;

import android.content.Context;
import android.graphics.Typeface;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.graphics.drawable.GradientDrawable;

import androidx.annotation.RequiresApi;
import androidx.appcompat.app.AppCompatActivity;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.util.ArrayList;

public class VoipFileManagerSuActivity extends AppCompatActivity {

    private ListView listView;
    private TextView pathView;
    private ArrayList<String> items = new ArrayList<>();
    private ArrayList<String> displayNames = new ArrayList<>();
    private FileAdapter adapter;

    // Audio UI components
    private LinearLayout audioControlPanel;
    private TextView currentFileView;
    private Button playPauseButton;
    private Button stopButton;
    private ProgressBar progressBar;
    private TextView timeView;

    // Audio playback state
    private AudioTrack currentTrack;
    private boolean isPlaying = false;
    private boolean isPaused = false;
    private String currentAudioFile = "";
    private Handler uiHandler = new Handler(Looper.getMainLooper());
    private Thread playbackThread;
    private AudioFocusRequest focusRequest;
    private AudioManager audioManager;
    private long totalDurationMs = 0;
    private long currentPositionMs = 0;
    private int originalVolume = 0;

    private String currentPath = "/sdcard/voip";

    // Hacker theme colors
    private static final int COLOR_BLACK = 0xFF000000;
    private static final int COLOR_DARK_GREEN = 0xFF001100;
    private static final int COLOR_BRIGHT_GREEN = 0xFF00FF00;
    private static final int COLOR_MATRIX_GREEN = 0xFF00CC00;
    private static final int COLOR_DARK_GRAY = 0xFF1A1A1A;
    private static final int COLOR_AMBER = 0xFFFFBF00;
    private static final int COLOR_CYAN = 0xFF00FFFF;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);

        // Create main layout with hacker theme
        LinearLayout mainLayout = new LinearLayout(this);
        mainLayout.setOrientation(LinearLayout.VERTICAL);
        mainLayout.setBackgroundColor(COLOR_BLACK);

        // Title bar - hacker style
        TextView titleView = new TextView(this);
        titleView.setText("VOIP FILE MANAGER");
        titleView.setTextColor(COLOR_BRIGHT_GREEN);
        titleView.setTextSize(18);
        titleView.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        titleView.setPadding(16, 16, 16, 8);
        titleView.setBackgroundColor(COLOR_DARK_GREEN);
        mainLayout.addView(titleView);

        // Path view - terminal style
        pathView = new TextView(this);
        pathView.setPadding(16, 8, 16, 8);
        pathView.setTextSize(12);
        pathView.setTextColor(COLOR_MATRIX_GREEN);
        pathView.setTypeface(Typeface.MONOSPACE);
        pathView.setBackgroundColor(COLOR_DARK_GRAY);
        mainLayout.addView(pathView);

        // File list with dark theme
        listView = new ListView(this);
        listView.setBackgroundColor(COLOR_BLACK);
        listView.setDivider(null);
        LinearLayout.LayoutParams listParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        mainLayout.addView(listView, listParams);

        // Audio control panel
        createAudioControlPanel();
        mainLayout.addView(audioControlPanel);

        setContentView(mainLayout);
        openDirectory(currentPath);
    }

    private void createAudioControlPanel() {
        audioControlPanel = new LinearLayout(this);
        audioControlPanel.setOrientation(LinearLayout.VERTICAL);
        audioControlPanel.setPadding(16, 16, 16, 16);
        audioControlPanel.setBackgroundColor(COLOR_DARK_GREEN);
        audioControlPanel.setVisibility(View.GONE);

        // Add border effect
        GradientDrawable border = new GradientDrawable();
        border.setColor(COLOR_DARK_GREEN);
        border.setStroke(2, COLOR_BRIGHT_GREEN);
        audioControlPanel.setBackground(border);

        // Header
        TextView headerView = new TextView(this);
        headerView.setText("AUDIO CONTROL TERMINAL ▶");
        headerView.setTextColor(COLOR_BRIGHT_GREEN);
        headerView.setTextSize(14);
        headerView.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        headerView.setPadding(0, 0, 0, 12);
        audioControlPanel.addView(headerView);

        // Current file display
        currentFileView = new TextView(this);
        currentFileView.setTextSize(14);
        currentFileView.setTextColor(COLOR_AMBER);
        currentFileView.setTypeface(Typeface.MONOSPACE);
        currentFileView.setPadding(0, 0, 0, 8);
        audioControlPanel.addView(currentFileView);

        // Progress bar with custom styling
        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(100);
        progressBar.setProgressTintList(android.content.res.ColorStateList.valueOf(COLOR_BRIGHT_GREEN));
        progressBar.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(COLOR_DARK_GRAY));
        LinearLayout.LayoutParams progressParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        progressParams.setMargins(0, 0, 0, 8);
        audioControlPanel.addView(progressBar, progressParams);

        // Time display
        timeView = new TextView(this);
        timeView.setTextSize(12);
        timeView.setText("00:00 / 00:00");
        timeView.setTextColor(COLOR_CYAN);
        timeView.setTypeface(Typeface.MONOSPACE);
        timeView.setPadding(0, 0, 0, 12);
        audioControlPanel.addView(timeView);

        // Control buttons
        LinearLayout buttonLayout = new LinearLayout(this);
        buttonLayout.setOrientation(LinearLayout.HORIZONTAL);

        playPauseButton = createHackerButton("▶ PLAY");
        playPauseButton.setOnClickListener(v -> togglePlayPause());

        stopButton = createHackerButton("◼ STOP");
        stopButton.setOnClickListener(v -> stopPlayback());

        LinearLayout.LayoutParams buttonParams = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        buttonParams.setMargins(0, 0, 8, 0);
        buttonLayout.addView(playPauseButton, buttonParams);

        buttonParams = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        buttonLayout.addView(stopButton, buttonParams);

        audioControlPanel.addView(buttonLayout);

        // Status info
        TextView statusLabel = new TextView(this);
        statusLabel.setText("STATUS: Use device volume controls");
        statusLabel.setPadding(0, 12, 0, 4);
        statusLabel.setTextSize(10);
        statusLabel.setTextColor(COLOR_MATRIX_GREEN);
        statusLabel.setTypeface(Typeface.MONOSPACE);
        audioControlPanel.addView(statusLabel);
    }

    private Button createHackerButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextColor(COLOR_BRIGHT_GREEN);
        button.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        button.setTextSize(12);

        // Custom button background
        GradientDrawable buttonBg = new GradientDrawable();
        buttonBg.setColor(COLOR_BLACK);
        buttonBg.setStroke(2, COLOR_BRIGHT_GREEN);
        buttonBg.setCornerRadius(4);
        button.setBackground(buttonBg);

        return button;
    }

    private void openDirectory(String path) {
        currentPath = path;
        pathView.setText("root@android:~$ ls " + path);

        new Thread(() -> {
            ArrayList<String> entries = new ArrayList<>();
            try {
                Process su = Runtime.getRuntime().exec(new String[]{"su", "-c", "ls -p " + path});
                InputStream is = su.getInputStream();

                int c;
                StringBuilder sb = new StringBuilder();
                while ((c = is.read()) != -1) {
                    if (c == '\n') {
                        String line = sb.toString().trim();
                        sb.setLength(0);
                        if (!line.isEmpty()) entries.add(line);
                    } else sb.append((char) c);
                }
                su.waitFor();
            } catch (Exception e) {
                runOnUiThread(() -> showHackerToast("ERROR: " + e.getMessage()));
            }

            runOnUiThread(() -> {
                items.clear();
                displayNames.clear();

                if (!"/sdcard/voip".equals(currentPath)) {
                    items.add("..");
                    displayNames.add("../");
                }

                items.addAll(entries);
                displayNames.addAll(entries);

                adapter = new FileAdapter(this, displayNames, items);
                listView.setAdapter(adapter);

                listView.setOnItemClickListener((parent, view, pos, id) -> {
                    String selected = items.get(pos);
                    if ("..".equals(selected)) {
                        File f = new File(currentPath);
                        openDirectory(f.getParent());
                    } else if (selected.endsWith("/")) {
                        openDirectory(currentPath + "/" + selected.replace("/", ""));
                    } else if (selected.endsWith(".ac") || selected.endsWith(".bc")) {
                        loadAudioFile(currentPath + "/" + selected);
                    } else {
                        showHackerToast("INVALID: Not an audio file");
                    }
                });
            });
        }).start();
    }

    private void showHackerToast(String message) {
        Toast toast = Toast.makeText(this, "◉ " + message + " ◉", Toast.LENGTH_SHORT);
        toast.show();
    }

    private void loadAudioFile(String fullPath) {
        stopPlayback();

        currentAudioFile = fullPath;
        String filename = new File(fullPath).getName();
        currentFileView.setText("LOADED: " + filename);
        audioControlPanel.setVisibility(View.VISIBLE);

        new Thread(() -> {
            try {
                String[] parts = filename.split("_");
                if (parts.length >= 3) {
                    int sampleRate = Integer.parseInt(parts[1]);
                    boolean isStereo = filename.endsWith(".ac");
                    byte[] data = runSuCat(fullPath);

                    int bytesPerFrame = isStereo ? 4 : 2;
                    int frameCount = data.length / bytesPerFrame;
                    totalDurationMs = isStereo ? (frameCount * 1000L) / sampleRate*2 : (frameCount * 1000L) / sampleRate;

                    android.util.Log.d("VoipPlayer", "Duration calc - Data: " + data.length + ", Stereo: " + isStereo +
                            ", Frames: " + frameCount + ", Rate: " + sampleRate + ", Duration: " + totalDurationMs + "ms");

                    runOnUiThread(() -> {
                        timeView.setText("00:00 / " + formatTime(totalDurationMs));
                        progressBar.setProgress(0);
                    });
                }
            } catch (Exception e) {
                android.util.Log.e("VoipPlayer", "Error calculating duration", e);
            }
        }).start();
    }

    private void togglePlayPause() {
        if (currentAudioFile.isEmpty()) {
            showHackerToast("ERROR: No audio file loaded");
            return;
        }

        if (isPlaying) {
            pausePlayback();
        } else {
            startPlayback();
        }
    }

    private void startPlayback() {
        if (isPaused && currentTrack != null) {
            currentTrack.play();
            isPlaying = true;
            playPauseButton.setText("⏸ PAUSE");
            startProgressUpdater();
        } else {
            playRawFile(currentAudioFile);
        }
    }

    private void pausePlayback() {
        if (currentTrack != null && isPlaying) {
            currentTrack.pause();
            isPlaying = false;
            isPaused = true;
            playPauseButton.setText("▶ PLAY");
        }
    }

    @RequiresApi(api = Build.VERSION_CODES.O)
    private void stopPlayback() {
        if (currentTrack != null) {
            currentTrack.stop();
            currentTrack.release();
            currentTrack = null;
        }

        if (focusRequest != null) {
            audioManager.abandonAudioFocusRequest(focusRequest);
        }

        if (originalVolume > 0) {
            audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, originalVolume, 0);
        }

        isPlaying = false;
        isPaused = false;
        currentPositionMs = 0;

        playPauseButton.setText("▶ PLAY");
        progressBar.setProgress(0);
        if (totalDurationMs > 0) {
            timeView.setText("00:00 / " + formatTime(totalDurationMs));
        }

        if (playbackThread != null) {
            playbackThread.interrupt();
        }
    }

    @RequiresApi(api = Build.VERSION_CODES.O)
    private void playRawFile(String fullPath) {
        String filename = new File(fullPath).getName();
        String[] parts = filename.split("_");
        if (parts.length < 3) {
            showHackerToast("INVALID: Bad filename format");
            return;
        }

        int sampleRate;
        try {
            sampleRate = Integer.parseInt(parts[1]);
        } catch (NumberFormatException e) {
            showHackerToast("ERROR: Invalid sample rate");
            return;
        }

        boolean isStereo = filename.endsWith(".ac");

        AudioAttributes focusAttributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                .build();

        focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
                .setAudioAttributes(focusAttributes)
                .build();

        int result = audioManager.requestAudioFocus(focusRequest);

        if (result != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            showHackerToast("ERROR: Could not get audio focus");
            return;
        }

        originalVolume = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC);
        int maxVolume = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, maxVolume, 0);

        playbackThread = new Thread(() -> {
            try {
                byte[] data = runSuCat(fullPath);

                int channelConfig = isStereo ? AudioFormat.CHANNEL_OUT_STEREO : AudioFormat.CHANNEL_OUT_MONO;
                int sr = isStereo ? sampleRate / 2 : sampleRate;

                android.util.Log.d("VoipPlayer", "Sample rate: " + sr + ", Stereo: " + isStereo + ", Data length: " + data.length);

                AudioAttributes attrs = new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                        .setFlags(AudioAttributes.FLAG_AUDIBILITY_ENFORCED)
                        .build();

                AudioFormat format = new AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(sr)
                        .setChannelMask(channelConfig)
                        .build();

                currentTrack = new AudioTrack(
                        attrs,
                        format,
                        data.length,
                        AudioTrack.MODE_STATIC,
                        AudioManager.AUDIO_SESSION_ID_GENERATE
                );

                currentTrack.setVolume(1.0f);
                int bytesWritten = currentTrack.write(data, 0, data.length);

                android.util.Log.d("VoipPlayer", "Bytes written: " + bytesWritten + " of " + data.length);

                if (bytesWritten != data.length) {
                    android.util.Log.e("VoipPlayer", "Failed to write all data");
                    runOnUiThread(() -> showHackerToast("ERROR: Failed to write audio data"));
                    return;
                }

                currentTrack.play();
                isPlaying = true;
                isPaused = false;

                runOnUiThread(() -> {
                    playPauseButton.setText("⏸ PAUSE");
                });

                startProgressUpdater();

                int bytesPerFrame = isStereo ? 4 : 2;
                int frameCount = data.length / bytesPerFrame;
                long durationMs = isStereo ? (frameCount * 1000L) / sr*2 : (frameCount * 1000L) / sr ;
                android.util.Log.d("VoipPlayer", "Playing for " + durationMs + "ms");

                Thread.sleep(durationMs + 100);

                if (!Thread.currentThread().isInterrupted()) {
                    runOnUiThread(() -> {
                        stopPlayback();
                        showHackerToast("COMPLETE: Playback finished");
                    });
                }

            } catch (InterruptedException e) {
                // Thread interrupted
            } catch (Exception e) {
                android.util.Log.e("VoipPlayer", "Playback error", e);
                runOnUiThread(() -> {
                    showHackerToast("ERROR: " + e.getMessage());
                    if (originalVolume > 0) {
                        audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, originalVolume, 0);
                    }
                    stopPlayback();
                });
            }
        });

        playbackThread.start();
    }

    private void startProgressUpdater() {
        uiHandler.post(new Runnable() {
            @Override
            public void run() {
                if (isPlaying && currentTrack != null) {
                    try {
                        int headPosition = currentTrack.getPlaybackHeadPosition();
                        if (headPosition >= 0 && totalDurationMs > 0) {
                            currentPositionMs = (headPosition * 1000L) / currentTrack.getSampleRate();

                            int progress = (int) ((currentPositionMs * 100) / totalDurationMs);
                            progressBar.setProgress(Math.min(progress, 100));

                            timeView.setText(formatTime(currentPositionMs) + " / " + formatTime(totalDurationMs));

                            android.util.Log.d("VoipProgress", "Head: " + headPosition + ", Current: " + currentPositionMs + "ms, Total: " + totalDurationMs + "ms");
                        }

                        if (isPlaying) {
                            uiHandler.postDelayed(this, 200);
                        }
                    } catch (Exception e) {
                        android.util.Log.e("VoipPlayer", "Progress update error", e);
                    }
                }
            }
        });
    }

    private String formatTime(long milliseconds) {
        long seconds = milliseconds / 1000;
        long minutes = seconds / 60;
        seconds = seconds % 60;
        return String.format("%02d:%02d", minutes, seconds);
    }

    private byte[] runSuCat(String path) throws Exception {
        Process su = Runtime.getRuntime().exec(new String[]{"su", "-c", "cat " + path});
        InputStream is = su.getInputStream();
        ByteArrayOutputStream bos = new ByteArrayOutputStream();

        byte[] buffer = new byte[4096];
        int read;
        while ((read = is.read(buffer)) != -1) {
            bos.write(buffer, 0, read);
        }

        su.waitFor();
        return bos.toByteArray();
    }

    @RequiresApi(api = Build.VERSION_CODES.O)
    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopPlayback();
    }

    // Custom adapter with hacker theme
    private static class FileAdapter extends ArrayAdapter<String> {
        private final ArrayList<String> items;
        private final Context ctx;

        FileAdapter(Context ctx, ArrayList<String> displayNames, ArrayList<String> items) {
            super(ctx, R.layout.item_file, displayNames);
            this.ctx = ctx;
            this.items = items;
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            if (convertView == null) {
                convertView = LayoutInflater.from(ctx).inflate(R.layout.item_file, parent, false);
            }

            TextView tv = convertView.findViewById(R.id.text);
            ImageView icon = convertView.findViewById(R.id.icon);

            String name = getItem(position);
            String fileName = items.get(position);

            // Apply hacker theme
            convertView.setBackgroundColor(COLOR_BLACK);
            tv.setText(name);
            tv.setTextColor(COLOR_MATRIX_GREEN);
            tv.setTypeface(Typeface.MONOSPACE);
            tv.setTextSize(12);

            // Set icon tint to green
            if ("..".equals(fileName)) {
                icon.setImageResource(android.R.drawable.ic_menu_revert);
                icon.setColorFilter(COLOR_BRIGHT_GREEN);
            } else if (fileName.endsWith("/")) {
                icon.setImageResource(android.R.drawable.ic_menu_agenda);
                icon.setColorFilter(COLOR_AMBER);
            } else if (fileName.endsWith(".ac") || fileName.endsWith(".bc")) {
                icon.setImageResource(android.R.drawable.ic_media_play);
                icon.setColorFilter(COLOR_CYAN);
            } else {
                icon.setImageResource(android.R.drawable.ic_menu_help);
                icon.setColorFilter(COLOR_MATRIX_GREEN);
            }

            return convertView;
        }
    }
}