#include <cassert>
#include "AudioRecord.h"
#pragma comment(lib, "Avrt.lib")

bool start;

namespace AUDIOLOOPBACK {

static std::queue<std::pair<BYTE *, int> > data_queue;
static std::mutex mutex_;
static std::condition_variable_any producer_;
static std::condition_variable_any consumer_;
static HANDLE g_hGetFreqEvent;

IMMDevice* GetDefaultDevice() {

	IMMDevice* pDevice = NULL;
	IMMDeviceEnumerator *pMMDeviceEnumerator = NULL;
	HRESULT hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pMMDeviceEnumerator);
	if (FAILED(hr)) return NULL;

	hr = pMMDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	pMMDeviceEnumerator->Release();

	return pDevice;
}

BOOL AdjustFormatTo16Bits(WAVEFORMATEX *pwfx) {
	BOOL bRet(FALSE);

	if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
	{
		pwfx->wFormatTag = WAVE_FORMAT_PCM;
		pwfx->wBitsPerSample = 16;
		pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
		pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;

		bRet = TRUE;
	}
	else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
		if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat))
		{
			pEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
			pEx->Samples.wValidBitsPerSample = 16;
			pwfx->wBitsPerSample = 16;
			pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
			pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;

			bRet = TRUE;
		}
	}

	return bRet;
}

UINT __stdcall DoCaptureAudio() {
	HRESULT hr;
	IAudioClient *pAudioClient = NULL;
	WAVEFORMATEX *pwfx = NULL;
	REFERENCE_TIME hnsDefaultDevicePeriod(0);
	HANDLE hTimerWakeUp = NULL;
	IAudioCaptureClient *pAudioCaptureClient = NULL;
	DWORD nTaskIndex = 0;
	HANDLE hTask = NULL;
	BOOL bStarted(FALSE);
	BYTE *pBuffer = NULL;
	BYTE *pBuffer_re = NULL;
	IMMDevice* pDeviceSndCard = GetDefaultDevice();
	do
	{
		hr = pDeviceSndCard->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
		if (FAILED(hr)) break;

		hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
		if (FAILED(hr)) break;

		hr = pAudioClient->GetMixFormat(&pwfx);
		if (FAILED(hr)) break;

		if (!AdjustFormatTo16Bits(pwfx)) break;

		hTimerWakeUp = CreateWaitableTimer(NULL, FALSE, NULL);
		if (hTimerWakeUp == NULL) break;



		hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pwfx, 0);
		if (FAILED(hr)) break;

		UINT32 bufferLength = 0;
		hr = pAudioClient->GetBufferSize(&bufferLength);
		if (FAILED(hr)) return 1;

		const UINT32 syncBufferSize = 2 * (bufferLength * (pwfx->nBlockAlign));
		int nBlockAlign = pwfx->nBlockAlign;
		pBuffer = new BYTE[syncBufferSize];
		int index = 0;
		if (pBuffer == NULL)
		{
			return (DWORD)E_POINTER;
		}

		hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pAudioCaptureClient);
		if (FAILED(hr)) break;

		hTask = AvSetMmThreadCharacteristics(L"Capture", &nTaskIndex);
		if (NULL == hTask) break;

		LARGE_INTEGER liFirstFire;
		liFirstFire.QuadPart = -hnsDefaultDevicePeriod / 2; // negative means relative time
		LONG lTimeBetweenFires = (LONG)hnsDefaultDevicePeriod / 2 / (10 * 1000); // convert to milliseconds

		BOOL bOK = SetWaitableTimer(hTimerWakeUp, &liFirstFire, lTimeBetweenFires, NULL, NULL, FALSE);
		if (!bOK) break;

		hr = pAudioClient->Start();
		if (FAILED(hr)) break;

		bStarted = TRUE;

		DWORD dwWaitResult;
		UINT32 nNextPacketSize(0);
		BYTE *pData = NULL;
		UINT32 nNumFramesToRead;
		DWORD dwFlags;

		WaitForSingleObject(g_hGetFreqEvent, INFINITY);

		int recBlockSize = pwfx->nSamplesPerSec / 100;

		while (start)
		{
			std::lock_guard<std::mutex> locker(mutex_);
			dwWaitResult = WaitForSingleObject(hTimerWakeUp, INFINITE);

			hr = pAudioCaptureClient->GetNextPacketSize(&nNextPacketSize);
			if (FAILED(hr))
			{
				break;
			}

			if (nNextPacketSize == 0) continue;

			hr = pAudioCaptureClient->GetBuffer(
				&pData,
				&nNumFramesToRead,
				&dwFlags,
				NULL,
				NULL
			);
			index += nNumFramesToRead;

			if (pData) {
				CopyMemory(&pBuffer[index * nBlockAlign], pData, nNumFramesToRead * nBlockAlign);
			}

			while (index >= recBlockSize) {

				pBuffer_re = new BYTE[nNumFramesToRead * nBlockAlign];
				if (pData)
				{
					memcpy(pBuffer, pData, nNumFramesToRead * nBlockAlign);
					memcpy(pBuffer_re, pData, nNumFramesToRead * nBlockAlign);
				}
				producer_.wait(mutex_, [&]()->bool {return data_queue.size() <= 10; });
				data_queue.push(std::make_pair(pBuffer_re, nNumFramesToRead * nBlockAlign));
				consumer_.notify_one();

				if (FAILED(hr))
				{
					break;
				}
				MoveMemory(&pBuffer[0], &pBuffer[recBlockSize * nBlockAlign], (index - recBlockSize) * nBlockAlign);
				index -= recBlockSize;
			}
			pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
		}

	} while (FALSE);

	if (hTask != NULL)
	{
		AvRevertMmThreadCharacteristics(hTask);
		hTask = NULL;
	}

	if (pAudioCaptureClient != NULL)
	{
		pAudioCaptureClient->Release();
		pAudioCaptureClient = NULL;
	}

	if (pwfx != NULL)
	{
		CoTaskMemFree(pwfx);
		pwfx = NULL;
	}

	if (hTimerWakeUp != NULL)
	{
		CancelWaitableTimer(hTimerWakeUp);
		CloseHandle(hTimerWakeUp);
		hTimerWakeUp = NULL;
	}

	if (pAudioClient != NULL)
	{
		if (bStarted)
		{
			pAudioClient->Stop();
		}

		pAudioClient->Release();
		pAudioClient = NULL;
	}

	if (pDeviceSndCard != NULL)
	{
		pDeviceSndCard->Release();
		pDeviceSndCard = NULL;
	}

	if (pDeviceSndCard != NULL)
	{
		CloseHandle(g_hGetFreqEvent);
		g_hGetFreqEvent = NULL;
	}

	return 0;
}

UINT __stdcall CaptureAudio(LPVOID param) {
	CoInitialize(NULL);

	UINT nRet = DoCaptureAudio();

	CoUninitialize();

	return nRet;
}

int16_t * GetAudio(int& nBytes) {
	std::lock_guard<std::mutex> locker(mutex_);
	consumer_.wait(mutex_, [&]()->bool {return data_queue.size() > 0; });
	int16_t *data_out = (int16_t *)data_queue.front().first;
	nBytes = data_queue.front().second;
	data_queue.pop();
	producer_.notify_one();
	return data_out;
}

int SaveAudio(int16_t *data, int len) {

	return 0;
}

AudioRecord::AudioRecord() :dur_time_(0), format_(""){
}

AudioRecord::AudioRecord(unsigned int dur_time, std::string format) {
	dur_time_ = dur_time;
	format = format_;
}

AudioRecord::~AudioRecord() {}

int AudioRecord::Start() {
	start = true;
	HANDLE hSetCaptureAudioThread = (HANDLE)_beginthreadex(NULL, 0, &CaptureAudio, NULL, 0, NULL);
	assert(hSetCaptureAudioThread != 0);
	g_hGetFreqEvent = CreateEvent(NULL, FALSE, FALSE, L"GetFreqEvent");
	return 0;
}

int AudioRecord::Stop() {
	start = false;
	return 0;
}

}