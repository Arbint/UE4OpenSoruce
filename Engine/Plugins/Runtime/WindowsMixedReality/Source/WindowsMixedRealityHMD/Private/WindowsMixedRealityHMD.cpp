// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealityHMD.h"

#include "WindowsMixedRealityStatics.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Interfaces/IPluginManager.h"
#include "IWindowsMixedRealityHMDPlugin.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#endif

#include "Engine/GameEngine.h"

//---------------------------------------------------
// Windows Mixed Reality HMD Plugin
//---------------------------------------------------

class FDepthConversionPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDepthConversionPS, Global);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	FDepthConversionPS() { }

	FDepthConversionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		// Bind shader inputs.
		FarPlaneDistance.Bind(Initializer.ParameterMap, TEXT("FarPlaneDistance"));
		InDepthTexture.Bind(Initializer.ParameterMap, TEXT("InDepthTexture"), SPF_Mandatory);
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
	}

	void SetParameters(FRHICommandList& RHICmdList, float farPlaneDistanceValue, FTextureRHIParamRef DepthTexture)
	{
		FPixelShaderRHIParamRef PixelShaderRHI = GetPixelShader();

		SetShaderValue(RHICmdList, PixelShaderRHI, FarPlaneDistance, farPlaneDistanceValue);

		FSamplerStateRHIParamRef SamplerStateRHI = TStaticSamplerState<SF_Point>::GetRHI();
		SetTextureParameter(RHICmdList, PixelShaderRHI, InDepthTexture, InTextureSampler, SamplerStateRHI, DepthTexture);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		// Serialize shader inputs.
		Ar << FarPlaneDistance;
		Ar << InDepthTexture;
		Ar << InTextureSampler;

		return bShaderHasOutdatedParameters;
	}

private:
	// Shader parameters.
	FShaderParameter FarPlaneDistance;
	FShaderResourceParameter InDepthTexture;
	FShaderResourceParameter InTextureSampler;
};

IMPLEMENT_SHADER_TYPE(, FDepthConversionPS, TEXT("/Plugin/WindowsMixedReality/Private/DepthConversion.usf"), TEXT("MainPixelShader"), SF_Pixel)

namespace WindowsMixedReality
{
	MixedRealityInterop hmd;

	class FWindowsMixedRealityHMDPlugin : public IWindowsMixedRealityHMDPlugin
	{
		/** IHeadMountedDisplayModule implementation */
		virtual TSharedPtr< class IXRTrackingSystem, ESPMode::ThreadSafe > CreateTrackingSystem() override;

		bool IsHMDConnected()
		{
			return WindowsMixedReality::hmd.IsAvailable();
		}

		FString GetModuleKeyName() const override
		{
			return FString(TEXT("WindowsMixedRealityHMD"));
		}

		void StartupModule() override
		{
			IHeadMountedDisplayModule::StartupModule();
			FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("WindowsMixedReality"))->GetBaseDir(), TEXT("Shaders"));
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/WindowsMixedReality"), PluginShaderDir);
		}

		void ShutdownModule() override
		{
			hmd.Dispose();
		}

		uint64 GetGraphicsAdapterLuid() override
		{
			return hmd.GraphicsAdapterLUID();
		}
	};

	TSharedPtr< class IXRTrackingSystem, ESPMode::ThreadSafe > FWindowsMixedRealityHMDPlugin::CreateTrackingSystem()
	{
		auto WindowsMRHMD = FSceneViewExtensions::NewExtension<WindowsMixedReality::FWindowsMixedRealityHMD>();
		if (WindowsMRHMD->IsInitialized())
		{
			return WindowsMRHMD;
		}
		return nullptr;
	}

	float FWindowsMixedRealityHMD::GetWorldToMetersScale() const
	{
		return CachedWorldToMetersScale;
	}

	//---------------------------------------------------
	// FWindowsMixedRealityHMD IHeadMountedDisplay Implementation
	//---------------------------------------------------

	bool FWindowsMixedRealityHMD::IsHMDConnected()
	{
		return hmd.IsAvailable();
	}

	bool FWindowsMixedRealityHMD::IsHMDEnabled() const
	{
		return true;
	}

	EHMDWornState::Type FWindowsMixedRealityHMD::GetHMDWornState()
	{
		MixedRealityInterop::UserPresence currentPresence = hmd.GetCurrentUserPresence();

		EHMDWornState::Type wornState = EHMDWornState::Type::Unknown;

		switch (currentPresence)
		{
		case MixedRealityInterop::UserPresence::Worn:
			wornState = EHMDWornState::Type::Worn;
			break;
		case MixedRealityInterop::UserPresence::NotWorn:
			wornState = EHMDWornState::Type::NotWorn;
			break;
		};

		return wornState;
	}

	void FWindowsMixedRealityHMD::OnBeginPlay(FWorldContext & InWorldContext)
	{
		EnableStereo(true);
	}

	void FWindowsMixedRealityHMD::OnEndPlay(FWorldContext & InWorldContext)
	{
		EnableStereo(false);
	}

	TRefCountPtr<ID3D11Device> FWindowsMixedRealityHMD::InternalGetD3D11Device()
	{
		TRefCountPtr<ID3D11Device> D3D11DeviceLocal;

		ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
			GetNativeDevice,
			TRefCountPtr<ID3D11Device>&, D3D11DeviceRef, D3D11DeviceLocal,
			{
				D3D11DeviceRef = (ID3D11Device*)RHIGetNativeDevice();
			});

		FlushRenderingCommands();

		return std::move(D3D11DeviceLocal);
	}

	/** Helper function for acquiring the appropriate FSceneViewport */
	FSceneViewport* FindMRSceneViewport(bool& allowStereo)
	{
		allowStereo = true;

		if (!GIsEditor)
		{
			UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);

			if (GameEngine->SceneViewport.Get() != nullptr)
			{
				allowStereo = GameEngine->SceneViewport.Get()->IsStereoRenderingAllowed();
			}

			return GameEngine->SceneViewport.Get();
		}
#if WITH_EDITOR
		else
		{
			UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GEngine);
			FSceneViewport* PIEViewport = (FSceneViewport*)EditorEngine->GetPIEViewport();
			if (PIEViewport != nullptr && PIEViewport->IsStereoRenderingAllowed())
			{
				// PIE is setup for stereo rendering
				allowStereo = PIEViewport->IsStereoRenderingAllowed();
				return PIEViewport;
			}
			else
			{
				// Check to see if the active editor viewport is drawing in stereo mode
				FSceneViewport* EditorViewport = (FSceneViewport*)EditorEngine->GetActiveViewport();
				if (EditorViewport != nullptr && EditorViewport->IsStereoRenderingAllowed())
				{
					allowStereo = EditorViewport->IsStereoRenderingAllowed();
					return EditorViewport;
				}
			}
		}
#endif

		allowStereo = false;
		return nullptr;
	}

	FString FWindowsMixedRealityHMD::GetVersionString() const
	{
		return FString(hmd.GetDisplayName());
	}

	void CenterMouse(RECT windowRect)
	{
		int width = windowRect.right - windowRect.left;
		int height = windowRect.bottom - windowRect.top;

		SetCursorPos(windowRect.left + width / 2, windowRect.top + height / 2);
	}

	bool FWindowsMixedRealityHMD::OnStartGameFrame(FWorldContext & WorldContext)
	{
		if (this->bRequestRestart)
		{
			this->bRequestRestart = false;

			ShutdownHolographic();
			EnableStereo(true);

			return true;
		}

		if (!hmd.IsInitialized())
		{
			D3D11Device = InternalGetD3D11Device();
			hmd.Initialize(D3D11Device.GetReference(),
				GNearClippingPlane / GetWorldToMetersScale(), farPlaneDistance);
			return true;
		}
		else
		{
			if (!hmd.IsImmersiveWindowValid())
			{
				// This can happen if the PC went to sleep.
				this->bRequestRestart = true;
				return true;
			}
		}

		if (!bIsStereoEnabled && bIsStereoDesired)
		{
			// Set up the HMD
			SetupHolographicCamera();
		}

		if (hmd.HasUserPresenceChanged())
		{
			currentWornState = GetHMDWornState();

			// Broadcast HMD worn/ not worn delegates.
			if (currentWornState == EHMDWornState::Worn)
			{
				FCoreDelegates::VRHeadsetPutOnHead.Broadcast();
			}
			else if (currentWornState == EHMDWornState::NotWorn)
			{
				FCoreDelegates::VRHeadsetRemovedFromHead.Broadcast();
			}
		}

		// Restore windows focus to game window to preserve keyboard/mouse input.
		if (currentWornState == EHMDWornState::Type::Worn)
		{
			HWND gameHWND = (HWND)GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle();

			// Set mouse focus to center of game window so any clicks interact with the game.
			if (mouseLockedToCenter)
			{
				RECT windowRect;
				GetWindowRect(gameHWND, &windowRect);

				CenterMouse(windowRect);
			}

			if (GetCapture() != gameHWND)
			{
				// Keyboard input
				SetForegroundWindow(gameHWND);

				// Mouse input
				SetCapture(gameHWND);
				SetFocus(gameHWND);

				FSlateApplication::Get().SetAllUserFocusToGameViewport();
			}
		}

		CachedWorldToMetersScale = WorldContext.World()->GetWorldSettings()->WorldToMeters;

		return true;
	}

	void FWindowsMixedRealityHMD::SetTrackingOrigin(EHMDTrackingOrigin::Type NewOrigin)
	{
		HMDTrackingOrigin = NewOrigin;
	}

	EHMDTrackingOrigin::Type FWindowsMixedRealityHMD::GetTrackingOrigin()
	{
		return HMDTrackingOrigin;
	}

	bool FWindowsMixedRealityHMD::EnumerateTrackedDevices(TArray<int32>& OutDevices, EXRTrackedDeviceType Type)
	{
		if (Type == EXRTrackedDeviceType::Any || Type == EXRTrackedDeviceType::HeadMountedDisplay)
		{
			OutDevices.Add(IXRTrackingSystem::HMDDeviceId);
			return true;
		}
		return false;
	}

	void FWindowsMixedRealityHMD::SetInterpupillaryDistance(float NewInterpupillaryDistance)
	{
		ipd = NewInterpupillaryDistance;
	}

	float FWindowsMixedRealityHMD::GetInterpupillaryDistance() const
	{
		if (ipd == 0)
		{
			return 0.064f;
		}

		return ipd;
	}

	bool FWindowsMixedRealityHMD::GetCurrentPose(
		int32 DeviceId,
		FQuat& CurrentOrientation,
		FVector& CurrentPosition)
	{
		if (DeviceId != IXRTrackingSystem::HMDDeviceId)
		{
			return false;
		}

		// Get most recently available tracking data.
		InitTrackingFrame();

		CurrentOrientation = CurrOrientation;
		CurrentPosition = CurrPosition;

		return true;
	}

	bool FWindowsMixedRealityHMD::GetRelativeEyePose(int32 DeviceId, EStereoscopicPass Eye, FQuat& OutOrientation, FVector& OutPosition)
	{
		OutOrientation = FQuat::Identity;
		OutPosition = FVector::ZeroVector;
		if (DeviceId == IXRTrackingSystem::HMDDeviceId && (Eye == eSSP_LEFT_EYE || Eye == eSSP_RIGHT_EYE))
		{
			OutPosition = FVector(0, (Eye == EStereoscopicPass::eSSP_LEFT_EYE ? 0.5f : -0.5f) * GetInterpupillaryDistance() * GetWorldToMetersScale(), 0);
			return true;
		}
		else
		{
			return false;
		}
	}

	void FWindowsMixedRealityHMD::ResetOrientationAndPosition(float yaw)
	{
		hmd.ResetOrientationAndPosition();
	}

	void FWindowsMixedRealityHMD::InitTrackingFrame()
	{
		DirectX::XMMATRIX leftPose;
		DirectX::XMMATRIX rightPose;
		MixedRealityInterop::HMDTrackingOrigin trackingOrigin;
		if (hmd.GetCurrentPose(leftPose, rightPose, trackingOrigin))
		{
			trackingOrigin == MixedRealityInterop::HMDTrackingOrigin::Eye ?
				SetTrackingOrigin(EHMDTrackingOrigin::Eye) :
				SetTrackingOrigin(EHMDTrackingOrigin::Floor);

			// Convert to unreal space
			FMatrix UPoseL = FWindowsMixedRealityStatics::ToFMatrix(leftPose);
			FMatrix UPoseR = FWindowsMixedRealityStatics::ToFMatrix(rightPose);
			RotationL = FQuat(UPoseL);
			RotationR = FQuat(UPoseR);

			RotationL = FQuat(-1 * RotationL.Z, RotationL.X, RotationL.Y, -1 * RotationL.W);
			RotationR = FQuat(-1 * RotationR.Z, RotationR.X, RotationR.Y, -1 * RotationR.W);

			RotationL.Normalize();
			RotationR.Normalize();

			FQuat HeadRotation = FMath::Lerp(RotationL, RotationR, 0.5f);
			HeadRotation.Normalize();

			// Position = forward/ backwards, left/ right, up/ down.
			PositionL = ((FVector(UPoseL.M[2][3], -1 * UPoseL.M[0][3], -1 * UPoseL.M[1][3]) * GetWorldToMetersScale()));
			PositionR = ((FVector(UPoseR.M[2][3], -1 * UPoseR.M[0][3], -1 * UPoseR.M[1][3]) * GetWorldToMetersScale()));

			PositionL = RotationL.RotateVector(PositionL);
			PositionR = RotationR.RotateVector(PositionR);

			if (ipd == 0)
			{
				ipd = FVector::Dist(PositionL, PositionR) / GetWorldToMetersScale();
			}

			FVector HeadPosition = FMath::Lerp(PositionL, PositionR, 0.5f);

			CurrOrientation = HeadRotation;
			CurrPosition = HeadPosition;
		}
	}

	void SetupHiddenVisibleAreaMesh(TArray<FHMDViewMesh>& HiddenMeshes, TArray<FHMDViewMesh>& VisibleMeshes)
	{
		for (int i = (int)MixedRealityInterop::HMDEye::Left;
			i <= (int)MixedRealityInterop::HMDEye::Right; i++)
		{
			MixedRealityInterop::HMDEye eye = (MixedRealityInterop::HMDEye)i;

			DirectX::XMFLOAT2* vertices;
			int length;
			if (hmd.GetHiddenAreaMesh(eye, vertices, length))
			{
				FVector2D* const vertexPositions = new FVector2D[length];
				for (int v = 0; v < length; v++)
				{
					// Remap to space Unreal is expecting.
					float x = (vertices[v].x + 1) / 2.0f;
					float y = (vertices[v].y + 1) / 2.0f;

					vertexPositions[v].Set(x, y);
				}
				HiddenMeshes[i].BuildMesh(vertexPositions, length, FHMDViewMesh::MT_HiddenArea);

				delete[] vertexPositions;
			}

			if (hmd.GetVisibleAreaMesh(eye, vertices, length))
			{
				FVector2D* const vertexPositions = new FVector2D[length];
				for (int v = 0; v < length; v++)
				{
					// Remap from NDC space to [0..1] bottom-left origin.
					float x = (vertices[v].x + 1) / 2.0f;
					float y = (vertices[v].y + 1) / 2.0f;

					vertexPositions[v].Set(x, y);
				}
				VisibleMeshes[i].BuildMesh(vertexPositions, length, FHMDViewMesh::MT_VisibleArea);

				delete[] vertexPositions;
			}
		}
	}

	void FWindowsMixedRealityHMD::SetupHolographicCamera()
	{
		// Set the viewport to match the HMD display
		FSceneViewport* SceneVP = FindMRSceneViewport(bIsStereoDesired);

		if (SceneVP)
		{
			TSharedPtr<SWindow> Window = SceneVP->FindWindow();
			if (Window.IsValid() && SceneVP->GetViewportWidget().IsValid())
			{
				if (bIsStereoDesired)
				{
					int Width = hmd.GetDisplayWidth();
					int Height = hmd.GetDisplayHeight();

					SceneVP->SetViewportSize(
						Width * 2,
						Height);

					Window->SetViewportSizeDrivenByWindow(false);

					bIsStereoEnabled = hmd.IsStereoEnabled();
					if (bIsStereoEnabled)
					{
						hmd.CreateHiddenVisibleAreaMesh();

						FWindowsMixedRealityHMD* Self = this;
						ENQUEUE_RENDER_COMMAND(SetupHiddenVisibleAreaMeshCmd)([Self](FRHICommandListImmediate& RHICmdList)
						{
							SetupHiddenVisibleAreaMesh(Self->HiddenAreaMesh, Self->VisibleAreaMesh);
						});
					}
				}
				else
				{
					FVector2D size = SceneVP->FindWindow()->GetSizeInScreen();
					SceneVP->SetViewportSize(size.X, size.Y);
					Window->SetViewportSizeDrivenByWindow(true);
					bIsStereoEnabled = false;
				}
			}
		}
		else if (GIsEditor && hmd.IsInitialized() && bIsStereoDesired && !bIsStereoEnabled)
		{
			// This can happen if device is disconnected while running in VR Preview, then create a new VR preview window while device is still disconnected.
			// We can get a window that is not configured for stereo when we plug our device back in.
			this->bRequestRestart = true;
		}

		// Uncap fps to enable FPS higher than 62
		GEngine->bForceDisableFrameRateSmoothing = bIsStereoEnabled;
	}

	bool FWindowsMixedRealityHMD::IsStereoEnabled() const
	{
		return bIsStereoEnabled;
	}

	bool FWindowsMixedRealityHMD::EnableStereo(bool stereo)
	{
		if (stereo)
		{
			if (hmd.IsInitialized())
			{
				return false;
			}

			FindMRSceneViewport(bIsStereoDesired);
			if (!bIsStereoDesired)
			{
				return false;
			}

			hmd.EnableStereo(stereo);

			InitializeHolographic();

			currentWornState = GetHMDWornState();

			FApp::SetUseVRFocus(true);
			FApp::SetHasVRFocus(true);
		}
		else
		{
			ShutdownHolographic();

			FApp::SetUseVRFocus(false);
			FApp::SetHasVRFocus(false);
		}

		return bIsStereoDesired;
	}

	void FWindowsMixedRealityHMD::AdjustViewRect(
		EStereoscopicPass StereoPass,
		int32& X, int32& Y,
		uint32& SizeX, uint32& SizeY) const
	{
		SizeX *= ScreenScalePercentage;
		SizeY *= ScreenScalePercentage;

		SizeX = SizeX / 2;
		if (StereoPass == eSSP_RIGHT_EYE)
		{
			X += SizeX;
		}
	}

	void FWindowsMixedRealityHMD::CalculateStereoViewOffset(const enum EStereoscopicPass StereoPassType, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation)
	{
		if (StereoPassType != eSSP_LEFT_EYE &&
			StereoPassType != eSSP_RIGHT_EYE)
		{
			return;
		}

		FVector HmdToEyeOffset = FVector::ZeroVector;

		FQuat CurEyeOrient;
		if (StereoPassType == eSSP_LEFT_EYE)
		{
			HmdToEyeOffset = PositionL - CurrPosition;
			CurEyeOrient = RotationL;
		}
		else if (StereoPassType == eSSP_RIGHT_EYE)
		{
			HmdToEyeOffset = PositionR - CurrPosition;
			CurEyeOrient = RotationR;
		}

		const FQuat ViewOrient = ViewRotation.Quaternion();
		const FQuat deltaControlOrientation = ViewOrient * CurEyeOrient.Inverse();
		const FVector vEyePosition = deltaControlOrientation.RotateVector(HmdToEyeOffset);
		ViewLocation += vEyePosition;
	}

	FMatrix FWindowsMixedRealityHMD::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const
	{
		if (StereoPassType != eSSP_LEFT_EYE &&
			StereoPassType != eSSP_RIGHT_EYE)
		{
			return FMatrix::Identity;
		}

		DirectX::XMMATRIX projection = (StereoPassType == eSSP_LEFT_EYE)
			? hmd.GetProjectionMatrix(MixedRealityInterop::HMDEye::Left)
			: hmd.GetProjectionMatrix(MixedRealityInterop::HMDEye::Right);

		auto result = FWindowsMixedRealityStatics::ToFMatrix(projection).GetTransposed();
		// Convert from RH to LH projection matrix
		// See PerspectiveOffCenterRH: https://msdn.microsoft.com/en-us/library/windows/desktop/ms918176.aspx
		result.M[2][0] *= -1;
		result.M[2][1] *= -1;
		result.M[2][2] *= -1;
		result.M[2][3] *= -1;

		// Switch to reverse-z, replace near and far distance
		const auto Nz = GNearClippingPlane;
		result.M[2][2] = 0.0f;
		result.M[3][2] = Nz;

		return result;
	}

	void FWindowsMixedRealityHMD::GetEyeRenderParams_RenderThread(
		const FRenderingCompositePassContext& Context,
		FVector2D& EyeToSrcUVScaleValue,
		FVector2D& EyeToSrcUVOffsetValue) const
	{
		if (Context.View.StereoPass == eSSP_LEFT_EYE)
		{
			EyeToSrcUVOffsetValue.X = 0.0f;
			EyeToSrcUVOffsetValue.Y = 0.0f;

			EyeToSrcUVScaleValue.X = 0.5f;
			EyeToSrcUVScaleValue.Y = 1.0f;
		}
		else
		{
			EyeToSrcUVOffsetValue.X = 0.5f;
			EyeToSrcUVOffsetValue.Y = 0.0f;

			EyeToSrcUVScaleValue.X = 0.5f;
			EyeToSrcUVScaleValue.Y = 1.0f;
		}
	}

	FIntPoint FWindowsMixedRealityHMD::GetIdealRenderTargetSize() const
	{
		int Width = hmd.GetDisplayWidth();
		int Height = hmd.GetDisplayHeight();

		return FIntPoint(Width * 2, Height);
	}

	//TODO: Spelling is intentional, overridden from IHeadMountedDisplay.h
	float FWindowsMixedRealityHMD::GetPixelDenity() const
	{
		check(IsInGameThread());
		return ScreenScalePercentage;
	}

	void FWindowsMixedRealityHMD::SetPixelDensity(const float NewDensity)
	{
		check(IsInGameThread());
		//TODO: Get actual minimum value from platform.
		ScreenScalePercentage = FMath::Clamp<float>(NewDensity, 0.4f, 1.0f);
	}

	// Called when screen size changes.
	void FWindowsMixedRealityHMD::UpdateViewportRHIBridge(bool bUseSeparateRenderTarget, const class FViewport& Viewport, FRHIViewport* const ViewportRHI)
	{
		if (IsStereoEnabled() && mCustomPresent != nullptr)
		{
			hmd.SetScreenScaleFactor(ScreenScalePercentage);
			mCustomPresent->UpdateViewport(Viewport, ViewportRHI);
		}
	}

	void FWindowsMixedRealityHMD::RenderTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* BackBuffer, class FRHITexture2D* SrcTexture, FVector2D WindowSize) const
	{
		const uint32 ViewportWidth = BackBuffer->GetSizeX();
		const uint32 ViewportHeight = BackBuffer->GetSizeY();

		const uint32 TextureWidth = SrcTexture->GetSizeX();
		const uint32 TextureHeight = SrcTexture->GetSizeY();

		const uint32 SourceWidth = TextureWidth / 2;
		const uint32 SourceHeight = TextureHeight;
		const float r1 = (float)ViewportWidth / (float)SourceWidth;
		const float r2 = (float)ViewportHeight / (float)SourceHeight;
		const float r = FMath::Min(r1, r2);
		const uint32 width = (float)SourceWidth * r;
		const uint32 height = (float)SourceHeight * r;
		const uint32 x = (ViewportWidth - width) * 0.5f;
		const uint32 y = (ViewportHeight - height) * 0.5f;

		SetRenderTarget(RHICmdList, BackBuffer, FTextureRHIRef());
		DrawClearQuad(RHICmdList, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		RHICmdList.SetViewport(x, y, 0, width + x, height + y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		const auto FeatureLevel = GMaxRHIFeatureLevel;
		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = RendererModule->GetFilterVertexDeclaration().VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SrcTexture);

		RendererModule->DrawRectangle(
			RHICmdList,
			0, 0,
			ViewportWidth, ViewportHeight,
			0.0f, 0.0f,
			0.5f, 1.0f,
			FIntPoint(ViewportWidth, ViewportHeight),
			FIntPoint(1, 1),
			*VertexShader,
			EDRF_Default);
	}

	// Create a BGRA backbuffer for rendering.
	bool FWindowsMixedRealityHMD::AllocateRenderTargetTexture(
		uint32 index,
		uint32 sizeX,
		uint32 sizeY,
		uint8 format,
		uint32 numMips,
		uint32 flags,
		uint32 targetableTextureFlags,
		FTexture2DRHIRef& outTargetableTexture,
		FTexture2DRHIRef& outShaderResourceTexture,
		uint32 numSamples)
	{
		if (!IsStereoEnabled())
		{
			return false;
		}

		FRHIResourceCreateInfo CreateInfo;

		// Since our textures must be BGRA, this plugin did require a change to WindowsD3D11Device.cpp
		// to add the D3D11_CREATE_DEVICE_BGRA_SUPPORT flag to the graphics device.
		RHICreateTargetableShaderResource2D(
			sizeX,
			sizeY,
			PF_B8G8R8A8, // must be BGRA
			numMips,
			flags,
			targetableTextureFlags,
			false,
			CreateInfo,
			outTargetableTexture,
			outShaderResourceTexture);

		CurrentBackBuffer = outTargetableTexture;

		return true;
	}

	bool FWindowsMixedRealityHMD::HasHiddenAreaMesh() const
	{
		return HiddenAreaMesh[0].IsValid() && HiddenAreaMesh[1].IsValid();
	}

	void FWindowsMixedRealityHMD::DrawHiddenAreaMesh_RenderThread(FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const
	{
		if (StereoPass == eSSP_FULL)
		{
			return;
		}

		int index = (StereoPass == eSSP_LEFT_EYE) ? 0 : 1;
		const FHMDViewMesh& Mesh = HiddenAreaMesh[index];
		check(Mesh.IsValid());

		RHICmdList.SetStreamSource(0, Mesh.VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(Mesh.IndexBufferRHI, PT_TriangleList, 0, 0, Mesh.NumVertices, 0, Mesh.NumTriangles, 1);
	}

	bool FWindowsMixedRealityHMD::HasVisibleAreaMesh() const
	{
		return VisibleAreaMesh[0].IsValid() && VisibleAreaMesh[1].IsValid();
	}

	void FWindowsMixedRealityHMD::DrawVisibleAreaMesh_RenderThread(FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const
	{
		if (StereoPass == eSSP_FULL)
		{
			return;
		}

		int index = (StereoPass == eSSP_LEFT_EYE) ? 0 : 1;
		const FHMDViewMesh& Mesh = VisibleAreaMesh[index];
		check(Mesh.IsValid());

		RHICmdList.SetStreamSource(0, Mesh.VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(Mesh.IndexBufferRHI, PT_TriangleList, 0, 0, Mesh.NumVertices, 0, Mesh.NumTriangles, 1);
	}

	void FWindowsMixedRealityHMD::SetupViewFamily(FSceneViewFamily& InViewFamily)
	{
		InViewFamily.EngineShowFlags.MotionBlur = 0;
		InViewFamily.EngineShowFlags.HMDDistortion = false;
		InViewFamily.EngineShowFlags.SetScreenPercentage(false);
		InViewFamily.EngineShowFlags.StereoRendering = IsStereoEnabled();
	}

	void FWindowsMixedRealityHMD::CreateHMDDepthTexture(FRHICommandListImmediate& RHICmdList)
	{
		check(IsInRenderingThread());

		// Update depth texture to match format Windows Mixed Reality platform is expecting.
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		FRHITexture2D* depthFRHITexture = SceneContext.GetSceneDepthTexture().GetReference()->GetTexture2D();

		const uint32 viewportWidth = depthFRHITexture->GetSizeX();
		const uint32 viewportHeight = depthFRHITexture->GetSizeY();

		bool recreateTextures = false;
		if (remappedDepthTexture != nullptr)
		{
			int width = remappedDepthTexture->GetSizeX();
			int height = remappedDepthTexture->GetSizeY();

			if (width != viewportWidth || height != viewportHeight)
			{
				recreateTextures = true;
			}
		}

		// Create a new texture for the remapped depth.
		if (remappedDepthTexture == nullptr || recreateTextures)
		{
			FRHIResourceCreateInfo CreateInfo;
			remappedDepthTexture = RHICmdList.CreateTexture2D(depthFRHITexture->GetSizeX(), depthFRHITexture->GetSizeY(),
				PF_R32_FLOAT, 1, 1, ETextureCreateFlags::TexCreate_RenderTargetable | ETextureCreateFlags::TexCreate_UAV, CreateInfo);
		}

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		SetRenderTarget(RHICmdList, remappedDepthTexture, FTextureRHIRef());
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		RHICmdList.SetViewport(0, 0, 0, viewportWidth, viewportHeight, 1.0f);

		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		const auto featureLevel = GMaxRHIFeatureLevel;
		auto shaderMap = GetGlobalShaderMap(featureLevel);

		TShaderMapRef<FScreenVS> vertexShader(shaderMap);
		TShaderMapRef<FDepthConversionPS> pixelShader(shaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = RendererModule->GetFilterVertexDeclaration().VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*vertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*pixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		pixelShader->SetParameters(RHICmdList, farPlaneDistance / GetWorldToMetersScale(), depthFRHITexture);

		RendererModule->DrawRectangle(
			RHICmdList,
			0, 0, // X, Y
			viewportWidth, viewportHeight, // SizeX, SizeY
			0.0f, 0.0f, // U, V
			1.0f, 1.0f, // SizeU, SizeV
			FIntPoint(viewportWidth, viewportHeight), // TargetSize
			FIntPoint(1, 1), // TextureSize
			*vertexShader,
			EDRF_Default);

		ID3D11Device* device = (ID3D11Device*)RHIGetNativeDevice();
		if (device == nullptr)
		{
			return;
		}

		// Create a new depth texture with 2 subresources for depth based reprojection.
		// Directly create an ID3D11Texture2D instead of an FTexture2D because we need an ArraySize of 2.
		if (stereoDepthTexture == nullptr || recreateTextures)
		{
			D3D11_TEXTURE2D_DESC tdesc;

			tdesc.Width = viewportWidth / 2;
			tdesc.Height = viewportHeight;
			tdesc.MipLevels = 1;
			tdesc.ArraySize = 2;
			tdesc.SampleDesc.Count = 1;
			tdesc.SampleDesc.Quality = 0;
			tdesc.Usage = D3D11_USAGE_DEFAULT;
			tdesc.Format = DXGI_FORMAT_R32_FLOAT;
			tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			tdesc.CPUAccessFlags = 0;
			tdesc.MiscFlags = 0;

			device->CreateTexture2D(&tdesc, NULL, &stereoDepthTexture);
		}

		stereoDepthTexture = (ID3D11Texture2D*)remappedDepthTexture->GetNativeResource();
	}

	void FWindowsMixedRealityHMD::PreRenderViewFamily_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FSceneViewFamily& InViewFamily)
	{
		if (!mCustomPresent || !hmd.IsInitialized() || !hmd.IsAvailable())
		{
			return;
		}

		CreateHMDDepthTexture(RHICmdList);
		if (!hmd.CreateRenderingParameters(stereoDepthTexture))
		{
			// This will happen if an exception is thrown while creating the frame's rendering parameters.
			// Because Windows Mixed Reality can only have 2 rendering parameters in flight at any time, this is fatal.
			this->bRequestRestart = true;
		}
	}

	bool FWindowsMixedRealityHMD::IsActiveThisFrame(class FViewport* InViewport) const
	{
		return GEngine && GEngine->IsStereoscopic3D(InViewport);
	}

	FWindowsMixedRealityHMD::FWindowsMixedRealityHMD(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
		, mCustomPresent(nullptr)
		, CurrOrientation(FQuat::Identity)
		, CurrPosition(FVector::ZeroVector)
		, HMDTrackingOrigin(EHMDTrackingOrigin::Floor)
		, ScreenScalePercentage(1.0f)
	{
		static const FName RendererModuleName("Renderer");
		RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);

		HiddenAreaMesh.SetNum(2);
		VisibleAreaMesh.SetNum(2);
	}

	FWindowsMixedRealityHMD::~FWindowsMixedRealityHMD()
	{
		ShutdownHolographic();
	}

	bool FWindowsMixedRealityHMD::IsInitialized() const
	{
		// Return true here because hmd needs an hwnd to initialize itself, but VR preview will not create a window if this is not true.
		return true;
	}

	// Cleanup resources needed for Windows Holographic view and tracking space.
	void FWindowsMixedRealityHMD::ShutdownHolographic()
	{
		check(IsInGameThread());

		hmd.EnableStereo(false);

		// Ensure that we aren't currently trying to render a frame before destroying our custom present.
		FlushRenderingCommands();
		StopCustomPresent();

		if (PauseHandle.IsValid())
		{
			FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(PauseHandle);
			PauseHandle.Reset();
		}

		bIsStereoDesired = false;
		bIsStereoEnabled = false;
	}

	bool FWindowsMixedRealityHMD::IsCurrentlyImmersive()
	{
		return hmd.IsCurrentlyImmersive();
	}

	// Setup Windows Holographic view and tracking space.
	void FWindowsMixedRealityHMD::InitializeHolographic()
	{
		if (!hmd.IsInitialized())
		{
			D3D11Device = InternalGetD3D11Device();
			if (D3D11Device != nullptr)
			{
				SetupHolographicCamera();
			}
		}

		static const auto screenPercentVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("vr.PixelDensity"));
		SetPixelDensity(screenPercentVar->GetValueOnGameThread());

		StartCustomPresent();

		// Hook into suspend/resume events
		if (!PauseHandle.IsValid())
		{
			PauseHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FWindowsMixedRealityHMD::AppServicePause);
		}
	}

	// Prevent crashes if computer goes to sleep.
	void FWindowsMixedRealityHMD::AppServicePause()
	{
		this->bRequestRestart = true;
	}

	bool WindowsMixedReality::FWindowsMixedRealityHMD::IsAvailable()
	{
		return hmd.IsAvailable();
	}

	// Initialize Windows Holographic present.
	void FWindowsMixedRealityHMD::StartCustomPresent()
	{
		if (mCustomPresent == nullptr)
		{
			mCustomPresent = new FWindowsMixedRealityCustomPresent(&hmd, D3D11Device);
		}
	}

	// Cleanup resources for holographic present.
	void FWindowsMixedRealityHMD::StopCustomPresent()
	{
		mCustomPresent = nullptr;
	}

	// Spatial Input
	bool FWindowsMixedRealityHMD::SupportsSpatialInput()
	{
		return hmd.SupportsSpatialInput();
	}

	MixedRealityInterop::HMDTrackingStatus FWindowsMixedRealityHMD::GetControllerTrackingStatus(MixedRealityInterop::HMDHand hand)
	{
		return hmd.GetControllerTrackingStatus(hand);
	}

	bool FWindowsMixedRealityHMD::GetControllerOrientationAndPosition(MixedRealityInterop::HMDHand hand, FRotator & OutOrientation, FVector & OutPosition)
	{
		DirectX::XMFLOAT4 rot;
		DirectX::XMFLOAT3 pos;
		if (hmd.GetControllerOrientationAndPosition(hand, rot, pos))
		{
			OutOrientation = FRotator(FWindowsMixedRealityStatics::FromMixedRealityQuaternion(rot));
			OutPosition = FWindowsMixedRealityStatics::FromMixedRealityVector(pos);

			return true;
		}

		return false;
	}

	void FWindowsMixedRealityHMD::PollInput()
	{
		hmd.PollInput();
	}

	MixedRealityInterop::HMDInputPressState WindowsMixedReality::FWindowsMixedRealityHMD::GetPressState(MixedRealityInterop::HMDHand hand, MixedRealityInterop::HMDInputControllerButtons button)
	{
		return hmd.GetPressState(hand, button);
	}

	float WindowsMixedReality::FWindowsMixedRealityHMD::GetAxisPosition(MixedRealityInterop::HMDHand hand, MixedRealityInterop::HMDInputControllerAxes axis)
	{
		return hmd.GetAxisPosition(hand, axis);
	}

	void WindowsMixedReality::FWindowsMixedRealityHMD::SubmitHapticValue(MixedRealityInterop::HMDHand hand, float value)
	{
		hmd.SubmitHapticValue(hand, value);
	}
}

IMPLEMENT_MODULE(WindowsMixedReality::FWindowsMixedRealityHMDPlugin, WindowsMixedRealityHMD)
