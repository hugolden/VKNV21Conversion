package com.android.example.rsmigration

import android.app.Activity
import android.content.res.AssetManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Build
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.ImageView
import java.io.ByteArrayOutputStream

class SampleActivity: Activity() {
    companion object {
        private val TAG = MainActivity::class.java.simpleName

        // We need two output images to avoid computing into an image currently on display.
        private const val NUMBER_OF_OUTPUT_IMAGES = 2

        // The maximum number of iterations to run for warmup.
        private const val MAX_WARMUP_ITERATIONS = 10

        // The maximum amount of time in ms to run for warmup.
        private const val MAX_WARMUP_TIME_MS = 1000.0

        // The maximum number of iterations to run during the benchmark.
        private const val MAX_BENCHMARK_ITERATIONS = 1000

        // The maximum amount of time in ms to run during the benchmark.
        private const val MAX_BENCHMARK_TIME_MS = 5000.0

        init {
            System.loadLibrary("rs_migration_jni")
        }
    }

    // Input and outputs
    private lateinit var mInputImage: Bitmap
    private var mCurrentOutputImageIndex = 0
    private lateinit var mConvertButton: Button
    private lateinit var mImageView:ImageView

    // Image processors
    private lateinit var mCurrentProcessor: VulkanImageProcessor

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_sample)
        mInputImage = loadBitmap(R.drawable.cliff)


        mCurrentProcessor = VulkanImageProcessor(this)

        mCurrentProcessor.configureNV21InputAndOutput(1920,1080,2);

        mConvertButton = findViewById(R.id.cvt_btn)

        mImageView = findViewById(R.id.output_img)

        mConvertButton.setOnClickListener(View.OnClickListener {
            var input = assets.open("out.nv21")
            var baos = ByteArrayOutputStream()
            var stagingBuffer =ByteArray(4096)
            var count = input.read(stagingBuffer)
            while(count>0){
                baos.write(stagingBuffer,0,count);
                count = input.read(stagingBuffer);
            }
            var buffer = baos.toByteArray()
            var outputBmp = mCurrentProcessor.convertFromNV21(buffer,1920,1080,mCurrentOutputImageIndex)
            mCurrentOutputImageIndex=(mCurrentOutputImageIndex+1)%2
            mImageView.setImageBitmap(outputBmp)

        })


    }

    private fun loadBitmap(resource: Int): Bitmap {
        val options = BitmapFactory.Options()
        options.inPreferredConfig = Bitmap.Config.ARGB_8888
        options.inScaled = false
        return BitmapFactory.decodeResource(resources, resource, options)
            ?: throw RuntimeException("Unable to load bitmap.")
    }
}