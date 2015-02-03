// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "LocalizationDashboardPrivatePCH.h"
#include "LocalizationCommandletExecution.h"
#include "Classes/LocalizationTarget.h"
#include "LocalizationConfigurationScript.h"
#include "ISourceControlModule.h"
#include "DesktopPlatformModule.h"
#include "SThrobber.h"
#include "Commandlets/CommandletHelpers.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "LocalizationCommandletExecutor"

namespace
{
	class SLocalizationCommandletExecutor : public SCompoundWidget
	{
		SLATE_BEGIN_ARGS(SLocalizationCommandletExecutor) {}
		SLATE_END_ARGS()

	private:
		struct FTaskListModel
		{
			enum class EState
			{
				Queued,
				InProgress,
				Failed,
				Succeeded
			};

			FTaskListModel()
				: State(EState::Queued)
			{
			}

			LocalizationCommandletExecution::FTask Task;
			EState State;
			FString LogOutput;
		};

		friend class STaskRow;

	public:
		SLocalizationCommandletExecutor();
		~SLocalizationCommandletExecutor();

		void Construct(const FArguments& Arguments, const TSharedRef<SWindow>& ParentWindow, const TArray<LocalizationCommandletExecution::FTask>& Tasks);
		void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
		bool WasSuccessful() const;
		void Log(const FString& String);

	private:
		void ExecuteCommandlet(const TSharedRef<FTaskListModel>& TaskListModel);
		void OnCommandletProcessCompletion(const int32 ReturnCode);
		void CancelCommandlet();
		void CleanUpProcessAndPump();

		bool HasCompleted() const;

		FText GetProgressMessageText() const;
		TOptional<float> GetProgressPercentage() const;

		TSharedRef<ITableRow> OnGenerateTaskListRow(TSharedPtr<FTaskListModel> TaskListModel, const TSharedRef<STableViewBase>& Table);
		TSharedPtr<FTaskListModel> GetCurrentTaskToView() const;

		FText GetLogString() const;

		FReply OnCopyLogClicked();
		void CopyLogToClipboard();

		FReply OnSaveLogClicked();

		FText GetCloseButtonText() const;
		FReply OnCloseButtonClicked();

	private:
		int32 CurrentTaskIndex;
		TArray< TSharedPtr<FTaskListModel> > TaskListModels;
		TSharedPtr<SProgressBar> ProgressBar;
		TSharedPtr< SListView< TSharedPtr<FTaskListModel> > > TaskListView;

		struct
		{
			FCriticalSection CriticalSection;
			FString String;
		} PendingData;


		TSharedPtr<SWindow> ParentWindow;
		TSharedPtr<FLocalizationCommandletProcess> CommandletProcess;
		FRunnable* Runnable;
		FRunnableThread* RunnableThread;
	};

	SLocalizationCommandletExecutor::SLocalizationCommandletExecutor()
		: CurrentTaskIndex(INDEX_NONE)
		, Runnable(nullptr)
		, RunnableThread(nullptr)
	{
	}

	SLocalizationCommandletExecutor::~SLocalizationCommandletExecutor()
	{
		CancelCommandlet();
	}

	void SLocalizationCommandletExecutor::Construct(const FArguments& Arguments, const TSharedRef<SWindow>& InParentWindow, const TArray<LocalizationCommandletExecution::FTask>& Tasks)
	{
		ParentWindow = InParentWindow;

		for (const LocalizationCommandletExecution::FTask& Task : Tasks)
		{
			const TSharedRef<FTaskListModel> Model = MakeShareable(new FTaskListModel());
			Model->Task = Task;
			TaskListModels.Add(Model);
		}

		TSharedPtr<SScrollBar> VerticalScrollBar;
		TSharedPtr<SScrollBar> HorizontalScrollBar;

		ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0, 16.0, 16.0, 0.0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(this, &SLocalizationCommandletExecutor::GetProgressMessageText)
					]

					+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0, 4.0, 0.0, 0.0)
						[
							SAssignNew(ProgressBar, SProgressBar)
							.Percent(this, &SLocalizationCommandletExecutor::GetProgressPercentage)
						]
				]
				+ SVerticalBox::Slot()
					.FillHeight(0.5)
					.Padding(0.0, 32.0, 8.0, 0.0)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(0.0f)
						[
							SAssignNew(TaskListView, SListView< TSharedPtr<FTaskListModel> >)
							.HeaderRow
							(
							SNew(SHeaderRow)
							+ SHeaderRow::Column("StatusIcon")
							.DefaultLabel(FText::GetEmpty())
							.FixedWidth(20.0)
							+ SHeaderRow::Column("TaskName")
							.DefaultLabel(LOCTEXT("TaskListNameColumnLabel", "Task"))
							.FillWidth(1.0)
							)
							.ListItemsSource(&TaskListModels)
							.OnGenerateRow(this, &SLocalizationCommandletExecutor::OnGenerateTaskListRow)
							.ItemHeight(24.0)
							.SelectionMode(ESelectionMode::Single)
						]
					]
				+ SVerticalBox::Slot()
					.FillHeight(0.5)
					.Padding(0.0, 32.0, 8.0, 0.0)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(0.0f)
						[
							SNew(SHorizontalBox)
							+SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(SVerticalBox)
								+SVerticalBox::Slot()
								.FillHeight(1.0f)
								[
									SNew(SMultiLineEditableText)
									.TextStyle(FEditorStyle::Get(), "LocalizationDashboard.CommandletLog.Text")
									.Text(this, &SLocalizationCommandletExecutor::GetLogString)
									.IsReadOnly(true)
									.HScrollBar(HorizontalScrollBar)
									.VScrollBar(VerticalScrollBar)
								]
								+SVerticalBox::Slot()
									.AutoHeight()
									[
										SAssignNew(HorizontalScrollBar, SScrollBar)
										.Orientation(EOrientation::Orient_Horizontal)
									]
							]
							+SHorizontalBox::Slot()
								.AutoWidth()
								[
									SAssignNew(VerticalScrollBar, SScrollBar)
									.Orientation(EOrientation::Orient_Vertical)
								]
						]
					]
				+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 5.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.ContentPadding(FMargin(6.0f, 2.0f))
							.Text(LOCTEXT("CopyLogButtonText", "Copy Log"))
							.ToolTipText(LOCTEXT("CopyLogButtonTooltip", "Copy the logged text to the clipboard."))
							.OnClicked(this, &SLocalizationCommandletExecutor::OnCopyLogClicked)
						]
						+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.ContentPadding(FMargin(6.0f, 2.0f))
								.IsEnabled(false)
								.Text(LOCTEXT("SaveLogButtonText", "Save Log..."))
								.ToolTipText(LOCTEXT("SaveLogButtonToolTip", "Save the logged text to a file."))
								.OnClicked(this, &SLocalizationCommandletExecutor::OnSaveLogClicked)
							]
						+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.ContentPadding(FMargin(6.0f, 2.0f))
								.OnClicked(this, &SLocalizationCommandletExecutor::OnCloseButtonClicked)
								[
									SNew(STextBlock)
									.Text(this, &SLocalizationCommandletExecutor::GetCloseButtonText)
								]
							]
					]
			];

		if(TaskListModels.Num() > 0)
		{
			CurrentTaskIndex = 0;
			ExecuteCommandlet(TaskListModels[CurrentTaskIndex].ToSharedRef());
		}
	}

	void SLocalizationCommandletExecutor::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		// Poll for log output data.
		if (!PendingData.String.IsEmpty())
		{
			FString String;

			// Copy the pending data string to the local string
			{
				FScopeLock ScopeLock(&PendingData.CriticalSection);
				String = PendingData.String;
				PendingData.String.Empty();
			}

			// Forward string to proper log.
			const TSharedPtr<FTaskListModel> CurrentTaskModel = TaskListModels[CurrentTaskIndex];
			CurrentTaskModel->LogOutput.Append(String);
		}

		// On Task Completed.
		if (CommandletProcess.IsValid())
		{
			FProcHandle CurrentProcessHandle = CommandletProcess->GetHandle();
			int32 ReturnCode;
			if (CurrentProcessHandle.IsValid() && FPlatformProcess::GetProcReturnCode(CurrentProcessHandle, &ReturnCode))
			{
				OnCommandletProcessCompletion(ReturnCode);
			}
		}
	}

	bool SLocalizationCommandletExecutor::WasSuccessful() const
	{
		return HasCompleted() && !TaskListModels.ContainsByPredicate([](const TSharedPtr<FTaskListModel>& TaskListModel){return TaskListModel->State != FTaskListModel::EState::Succeeded;});
	}

	void SLocalizationCommandletExecutor::Log(const FString& String)
	{
		FScopeLock ScopeLock(&PendingData.CriticalSection);
		PendingData.String += String;
	}

	void SLocalizationCommandletExecutor::OnCommandletProcessCompletion(const int32 ReturnCode)
	{
		CleanUpProcessAndPump();

		// Handle return code.
		TSharedPtr<FTaskListModel> CurrentTaskModel = TaskListModels[CurrentTaskIndex];
		// Zero code is successful.
		if (ReturnCode == 0)
		{
			CurrentTaskModel->State = FTaskListModel::EState::Succeeded;

			++CurrentTaskIndex;

			// Begin new task if possible.
			if (TaskListModels.IsValidIndex(CurrentTaskIndex))
			{
				CurrentTaskModel = TaskListModels[CurrentTaskIndex];
				ExecuteCommandlet(CurrentTaskModel.ToSharedRef());
			}
		}
		// Non-zero is a failure.
		else
		{
			CurrentTaskModel->State = FTaskListModel::EState::Failed;
		}
	}

	void SLocalizationCommandletExecutor::ExecuteCommandlet(const TSharedRef<FTaskListModel>& TaskListModel)
	{
		CommandletProcess = FLocalizationCommandletProcess::Execute(TaskListModel->Task.ScriptPath);
		if (CommandletProcess.IsValid())
		{
			TaskListModel->State = FTaskListModel::EState::InProgress;
		}
		else
		{
			TaskListModel->State = FTaskListModel::EState::Failed;
			return;
		}

		class FCommandletLogPump : public FRunnable
		{
		public:
			FCommandletLogPump(void* const InReadPipe, const FProcHandle& InCommandletProcessHandle, const TSharedRef<SLocalizationCommandletExecutor>& InCommandletWidget)
				: ReadPipe(InReadPipe)
				, CommandletProcessHandle(InCommandletProcessHandle)
				, CommandletWidget(InCommandletWidget)
			{
			}

			uint32 Run() override
			{
				for(;;)
				{
					// Read from pipe.
					const FString PipeString = FPlatformProcess::ReadPipe(ReadPipe);

					// Process buffer.
					if (!PipeString.IsEmpty())
					{
						// Add strings to log.
						const TSharedPtr<SLocalizationCommandletExecutor> Widget = CommandletWidget.Pin();
						if (Widget.IsValid())
						{
							Widget->Log(PipeString);
						}
					}

					// If the process isn't running and there's no data in the pipe, we're done.
					if (!FPlatformProcess::IsProcRunning(CommandletProcessHandle) && PipeString.IsEmpty())
					{
						break;
					}

					// Sleep.
					FPlatformProcess::Sleep(0.0f);
				}

				int32 ReturnCode = 0;
				return FPlatformProcess::GetProcReturnCode(CommandletProcessHandle, &ReturnCode) ? ReturnCode : -1;
			}

		private:
			void* const ReadPipe;
			FProcHandle CommandletProcessHandle;
			const TWeakPtr<SLocalizationCommandletExecutor> CommandletWidget;
		};

		// Launch runnable thread.
		Runnable = new FCommandletLogPump(CommandletProcess->GetReadPipe(), CommandletProcess->GetHandle(), SharedThis(this));
		RunnableThread = FRunnableThread::Create(Runnable, TEXT("Localization Commandlet Log Pump Thread"));
	}

	void SLocalizationCommandletExecutor::CancelCommandlet()
	{
		CleanUpProcessAndPump();
	}

	void SLocalizationCommandletExecutor::CleanUpProcessAndPump()
	{
		if (CommandletProcess.IsValid())
		{
			FProcHandle CommandletProcessHandle = CommandletProcess->GetHandle();
			if (CommandletProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(CommandletProcessHandle))
			{
				FPlatformProcess::TerminateProc(CommandletProcessHandle, true);
			}
			CommandletProcess.Reset();
		}

		if (RunnableThread)
		{
			RunnableThread->WaitForCompletion();
			delete RunnableThread;
			RunnableThread = nullptr;
		}

		if (Runnable)
		{
			delete Runnable;
			Runnable = nullptr;
		}

	}

	bool SLocalizationCommandletExecutor::HasCompleted() const
	{
		return CurrentTaskIndex == TaskListModels.Num();
	}

	FText SLocalizationCommandletExecutor::GetProgressMessageText() const
	{
		return TaskListModels.IsValidIndex(CurrentTaskIndex) ? TaskListModels[CurrentTaskIndex]->Task.Name : FText::GetEmpty();
	}

	TOptional<float> SLocalizationCommandletExecutor::GetProgressPercentage() const
	{
		return TOptional<float>(float(CurrentTaskIndex) / float(TaskListModels.Num()));
	}

	class STaskRow : public SMultiColumnTableRow< TSharedPtr<SLocalizationCommandletExecutor::FTaskListModel> >
	{
	public:
		void Construct(const FTableRowArgs& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedRef<SLocalizationCommandletExecutor::FTaskListModel>& InTaskListModel);
		TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName);

	private:
		FSlateColor HandleIconColorAndOpacity() const;
		const FSlateBrush* HandleIconImage() const;
		EVisibility HandleThrobberVisibility() const;

	private:
		TSharedPtr<SLocalizationCommandletExecutor::FTaskListModel> TaskListModel;
	};

	void STaskRow::Construct(const FTableRowArgs& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedRef<SLocalizationCommandletExecutor::FTaskListModel>& InTaskListModel)
	{
		TaskListModel = InTaskListModel;

		FSuperRowType::Construct(InArgs, OwnerTableView);
	}

	TSharedRef<SWidget> STaskRow::GenerateWidgetForColumn(const FName& ColumnName)
	{
		if (ColumnName == "StatusIcon")
		{
			return SNew(SOverlay)

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SThrobber)
					.Animate(SThrobber::VerticalAndOpacity)
					.NumPieces(1)
					.Visibility(this, &STaskRow::HandleThrobberVisibility)
				]
			+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.ColorAndOpacity(this, &STaskRow::HandleIconColorAndOpacity)
					.Image(this, &STaskRow::HandleIconImage)
				];
		}
		else if (ColumnName == "TaskName")
		{
			return SNew(STextBlock)
				.Text(TaskListModel->Task.Name);
		}
		else
		{
			return SNullWidget::NullWidget;
		}
	}

	FSlateColor STaskRow::HandleIconColorAndOpacity( ) const
	{
		if (TaskListModel.IsValid())
		{
			switch(TaskListModel->State)
			{
			case SLocalizationCommandletExecutor::FTaskListModel::EState::InProgress:
				return FLinearColor::Yellow;
				break;
			case SLocalizationCommandletExecutor::FTaskListModel::EState::Succeeded:
				return FLinearColor::Green;
				break;
			case SLocalizationCommandletExecutor::FTaskListModel::EState::Failed:
				return FLinearColor::Red;
				break;
			}
		}

		return FSlateColor::UseForeground();
	}

	const FSlateBrush* STaskRow::HandleIconImage( ) const
	{
		if (TaskListModel.IsValid())
		{
			switch(TaskListModel->State)
			{
			case SLocalizationCommandletExecutor::FTaskListModel::EState::Succeeded:
				return FEditorStyle::GetBrush("Symbols.Check");
				break;
			case SLocalizationCommandletExecutor::FTaskListModel::EState::Failed:
				return FEditorStyle::GetBrush("Icons.Cross");
				break;
			}
		}

		return NULL;
	}

	EVisibility STaskRow::HandleThrobberVisibility( ) const
	{
		if (TaskListModel.IsValid())
		{
			switch(TaskListModel->State)
			{
			case SLocalizationCommandletExecutor::FTaskListModel::EState::InProgress:
				return EVisibility::Visible;
				break;
			}
		}

		return EVisibility::Hidden;
	}

	TSharedRef<ITableRow> SLocalizationCommandletExecutor::OnGenerateTaskListRow(TSharedPtr<FTaskListModel> TaskListModel, const TSharedRef<STableViewBase>& Table)
	{
		return SNew(STaskRow, Table, TaskListModel.ToSharedRef());
	}

	TSharedPtr<SLocalizationCommandletExecutor::FTaskListModel> SLocalizationCommandletExecutor::GetCurrentTaskToView() const
	{
		if (TaskListView.IsValid())
		{
			const TArray< TSharedPtr<FTaskListModel> > Selection = TaskListView->GetSelectedItems();
			return Selection.Num() > 0 ? Selection.Top() : nullptr;
		}
		return nullptr;
	}

	FText SLocalizationCommandletExecutor::GetLogString() const
	{
		const TSharedPtr<SLocalizationCommandletExecutor::FTaskListModel> TaskToView = GetCurrentTaskToView();
		return TaskToView.IsValid() ? FText::FromString(TaskToView->LogOutput) : FText::GetEmpty();
	}

	FReply SLocalizationCommandletExecutor::OnCopyLogClicked()
	{
		CopyLogToClipboard();

		return FReply::Handled();
	}

	void SLocalizationCommandletExecutor::CopyLogToClipboard()
	{
		FPlatformMisc::ClipboardCopy(*(GetLogString().ToString()));
	}

	FReply SLocalizationCommandletExecutor::OnSaveLogClicked()
	{
		const FString TextFileDescription = LOCTEXT("TextFileDescription", "Text File").ToString();
		const FString TextFileExtension = TEXT("txt");
		const FString TextFileExtensionWildcard = FString::Printf(TEXT("*.%s"), *TextFileExtension);
		const FString FileTypes = FString::Printf(TEXT("%s (%s)|%s"), *TextFileDescription, *TextFileExtensionWildcard, *TextFileExtensionWildcard);
		const FString DefaultFilename = FString::Printf(TEXT("%s.%s"), TEXT("Log"), *TextFileExtension);
		const FString DefaultPath = FPaths::GameSavedDir();

		TArray<FString> SaveFilenames;
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

		// Prompt the user for the filename
		if (DesktopPlatform)
		{
			void* ParentWindowWindowHandle = NULL;

			const TSharedPtr<SWindow>& ParentWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));
			if (ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid())
			{
				ParentWindowWindowHandle = ParentWindow->GetNativeWindow()->GetOSWindowHandle();
			}

			if (DesktopPlatform->SaveFileDialog(
				ParentWindowWindowHandle,
				LOCTEXT("SaveLogDialogTitle", "Save Log to File").ToString(),
				DefaultPath,
				DefaultFilename,
				FileTypes,
				EFileDialogFlags::None,
				SaveFilenames
				))
			{
				// Save to file.
				FFileHelper::SaveStringToFile( GetLogString().ToString(), *(SaveFilenames.Last()) );
			}
		}

		return FReply::Handled();
	}

	FText SLocalizationCommandletExecutor::GetCloseButtonText() const
	{
		return HasCompleted() ? LOCTEXT("OkayButtonText", "Okay") : LOCTEXT("CancelButtonText", "Cancel");
	}

	FReply SLocalizationCommandletExecutor::OnCloseButtonClicked()
	{
		if (!HasCompleted())
		{
			CancelCommandlet();
		}
		ParentWindow->RequestDestroyWindow();
		return FReply::Handled();
	}
}

bool LocalizationCommandletExecution::Execute(const TSharedRef<SWindow>& ParentWindow, const FText& Title, const TArray<FTask>& Tasks)
{
	const TSharedRef<SWindow> CommandletWindow = SNew(SWindow)
		.Title(Title)
		.SupportsMinimize(false)
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.ClientSize(FVector2D(600,400))
		.ActivateWhenFirstShown(true)
		.FocusWhenFirstShown(true);
	const TSharedRef<SLocalizationCommandletExecutor> CommandletExecutor = SNew(SLocalizationCommandletExecutor, CommandletWindow, Tasks);
	CommandletWindow->SetContent(CommandletExecutor);

	FSlateApplication::Get().AddModalWindow(CommandletWindow, ParentWindow, false);

	return CommandletExecutor->WasSuccessful();
}

TSharedPtr<FLocalizationCommandletProcess> FLocalizationCommandletProcess::Execute(const FString& ConfigFilePath)
{
	// Create pipes.
	void* ReadPipe;
	void* WritePipe;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		return nullptr;
	}

	// Create process.
	FString CommandletArguments;

	const FString ConfigFileRelativeToGameDir = LocalizationConfigurationScript::MakePathRelativeToProjectDirectory(ConfigFilePath);
	CommandletArguments = FString::Printf( TEXT("-config=\"%s\""), *ConfigFileRelativeToGameDir );

	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	if (SourceControlModule.IsEnabled())
	{
		if (!SourceControlModule.GetProvider().IsAvailable())
		{
			CommandletArguments = FString::Printf( TEXT("%s -EnableSCC -DisableSCCSubmit"), *CommandletArguments );
		}
	}

	FProcHandle CommandletProcessHandle = CommandletHelpers::CreateCommandletProcess(TEXT("GatherText"), *FPaths::GetProjectFilePath(), *CommandletArguments, true, true, true, nullptr, 0, nullptr, WritePipe);

	// Close pipes if process failed.
	if (!CommandletProcessHandle.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		return nullptr;
	}

	return MakeShareable(new FLocalizationCommandletProcess(ReadPipe, WritePipe, CommandletProcessHandle));
}

FLocalizationCommandletProcess::~FLocalizationCommandletProcess()
{
	if (FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		FPlatformProcess::TerminateProc(ProcessHandle);
	}
	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
}

#undef LOCTEXT_NAMESPACE