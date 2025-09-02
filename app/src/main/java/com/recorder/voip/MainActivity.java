package com.recorder.voip;

import androidx.appcompat.app.AppCompatActivity;

import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;

import com.recorder.voip.databinding.ActivityMainBinding;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends AppCompatActivity {

    // Used to load the 'voip' library on application startup.
    static {
        System.loadLibrary("voip");
    }

    public static String copyAssets(Context context, String assetPath, File outFile) throws IOException {
        try (InputStream in = context.getAssets().open(assetPath);
             OutputStream out = new FileOutputStream(outFile)) {

            byte[] buf = new byte[4096];
            int len;
            while ((len = in.read(buf)) > 0) {
                out.write(buf, 0, len);
            }
        }

        // Make it executable
        outFile.setExecutable(true, true);

        return outFile.getAbsolutePath(); // return full path
    }


    public static String copyProcessInjector(Context context) throws IOException {
        // Detect ABI (e.g., "arm64-v8a")
        String abi = Build.SUPPORTED_ABIS[0];
        String assetPath = abi + "/AndKittyInjector";

        File outFile = new File(context.getFilesDir(), "AndKittyInjector");
        return copyAssets(context, assetPath, outFile);
    }

    public static String copySepolicyInjector(Context context) throws IOException {
        // Detect ABI (e.g., "arm64-v8a")
        String abi = Build.SUPPORTED_ABIS[0];
        String assetPath = abi + "/sepolicy-inject";

        File outFile = new File(context.getFilesDir(), "sepolicy-inject");
        return copyAssets(context, assetPath, outFile);
    }


    private ActivityMainBinding binding;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        try {
            copyProcessInjector(this);
            copySepolicyInjector(this);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        binding.btnInjectPolicies.setOnClickListener(v -> {
            binding.tvStatus.setText("Injecting Policies. Wait...");
            new Thread(() -> {
                String status = injectPolicies(); // JNI call
                runOnUiThread(() -> binding.tvStatus.setText(status));
            }).start();
        });

        binding.btnStartMonitoring.setOnClickListener(v -> {
            binding.tvStatus.setText("Starting Monitoring. Wait...");
            new Thread(() -> {
                String status = startMonitoring();
                runOnUiThread(() -> binding.tvStatus.setText(status));
            }).start();
        });

        binding.btnStopMonitoring.setOnClickListener(v -> {
            binding.tvStatus.setText("Stopping Monitoring. Wait...");
            new Thread(() -> {
                String status = stopMonitoring();
                runOnUiThread(() -> binding.tvStatus.setText(status));
            }).start();
        });

        binding.btnCopyData.setOnClickListener(v -> {
            binding.tvStatus.setText("Copying Data. Wait...");
            new Thread(() -> {
                String status = moveDataToSdcard();
                runOnUiThread(() -> binding.tvStatus.setText(status));
            }).start();
        });

        binding.btnViewLogs.setOnClickListener(v -> {
            Intent intent = new Intent(MainActivity.this, LogsActivity.class);
            startActivity(intent);
        });

        binding.btnEnableSelinux.setOnClickListener(v -> {
            new Thread(() -> {
                String status = enableSelinux();
                runOnUiThread(() -> binding.tvStatus.setText(status));
            }).start();
        });

        binding.btnDisableSelinux.setOnClickListener(v -> {
            new Thread(() -> {
                String status = disableSelinux();
                runOnUiThread(() -> binding.tvStatus.setText(status));
            }).start();
        });

        binding.btnDeleteData.setOnClickListener(v -> {
            binding.tvStatus.setText("Deleting Data. Wait...");
            new Thread(() -> {
                String status = deleteData();
                runOnUiThread(() -> binding.tvStatus.setText(status));
            }).start();
        });


    }


    private native String startMonitoring();
    private native String stopMonitoring();
    private native String moveDataToSdcard();
    private native String enableSelinux();
    private native String disableSelinux();
    private native String injectPolicies();
    private native String deleteData();
}
