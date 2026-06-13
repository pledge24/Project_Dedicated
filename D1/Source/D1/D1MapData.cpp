// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1MapData.h"

bool UD1MapData::BuildLayout(FD1MapLayout& OutLayout, FString& OutError) const
{
	OutLayout = FD1MapLayout();

	const int32 Height = Rows.Num();
	if (Height < 3)
	{
		OutError = FString::Printf(TEXT("Rows=%d (행이 3 미만)"), Height);
		return false;
	}

	const int32 Width = Rows[0].Len();
	if (Width < 3)
	{
		OutError = FString::Printf(TEXT("Width=%d (열이 3 미만)"), Width);
		return false;
	}

	OutLayout.GridSize = FIntPoint(Width, Height);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		const FString& Row = Rows[Y];
		if (Row.Len() != Width)
		{
			OutError = FString::Printf(TEXT("행 %d 길이 %d != %d (직사각형 아님)"), Y, Row.Len(), Width);
			return false;
		}

		for (int32 X = 0; X < Width; ++X)
		{
			const TCHAR C = Row[X];
			const FIntPoint Cell(X, Y);

			switch (C)
			{
			case TEXT('#'):
				OutLayout.WallCells.Add(Cell);
				break;

			case TEXT('o'):
			case TEXT('O'):
				OutLayout.SoftBlockCells.Add(Cell);
				break;

			case TEXT('.'):
			case TEXT(' '):
				break; // 빈칸

			default:
				// '1'~'4'(이상)는 스폰 지점, 슬롯 = 숫자-1. 그 외 문자는 빈칸 취급.
				if (C >= TEXT('1') && C <= TEXT('9'))
				{
					FD1MapStart Start;
					Start.Cell = Cell;
					Start.Slot = static_cast<int32>(C - TEXT('1'));
					OutLayout.Starts.Add(Start);
				}
				break;
			}
		}
	}

	return true;
}
