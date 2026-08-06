// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/Map/D1MapData.h"

#include "Framework/D1MatchTypes.h"

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

	bool bSlotUsed[D1MaxPlayerSlots] = {};

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
				// '1'~'9'는 스폰 문자로 파싱 — 슬롯 범위(1~4) 초과는 오타로 보고 에러. 그 외 문자는 빈칸 취급.
				if (C >= TEXT('1') && C <= TEXT('9'))
				{
					const int32 Slot = static_cast<int32>(C - TEXT('1'));
					if (Slot >= D1MaxPlayerSlots)
					{
						OutError = FString::Printf(TEXT("스폰 문자 '%c'가 슬롯 범위 초과(0~%d만 허용) @ (%d,%d)"), C, D1MaxPlayerSlots - 1, X, Y);
						return false;
					}
					if (bSlotUsed[Slot])
					{
						OutError = FString::Printf(TEXT("스폰 슬롯 %d 중복 @ (%d,%d)"), Slot, X, Y);
						return false;
					}
					bSlotUsed[Slot] = true;

					FD1MapStart Start;
					Start.Cell = Cell;
					Start.Slot = Slot;
					OutLayout.Starts.Add(Start);
				}
				break;
			}
		}
	}

	return true;
}
