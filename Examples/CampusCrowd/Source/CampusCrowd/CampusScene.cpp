#include "CampusScene.h"
#include "ProceduralMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Materials/MaterialInterface.h"

ACampusScene::ACampusScene()
{
    Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CAD campus geometry"));
    Mesh->SetFlags(RF_Transient);
    RootComponent = Mesh;
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ACampusScene::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    if (!IsTemplate() && Mesh)
    {
        SetRootComponent(Mesh);
        if (Nodes.IsEmpty()) LoadCampus();
    }
}
void ACampusScene::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (Nodes.IsEmpty()) LoadCampus();
}
void ACampusScene::BeginPlay()
{
    Super::BeginPlay();
    if (Nodes.IsEmpty()) LoadCampus();
}
bool ACampusScene::LoadCampus()
{
    Nodes.Empty();
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Campus/campus.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    {
        LoadError = TEXT("Missing or invalid Content/Campus/campus.json. Run Tools/import_campus.py.");
        UE_LOG(LogTemp, Error, TEXT("%s"), *LoadError);
        return false;
    }
    LoadError.Empty();
    Extent = Root->GetNumberField(TEXT("extent"));
    BuildingCount = Root->GetNumberField(TEXT("building_count"));
    for (auto Value : Root->GetArrayField(TEXT("nodes")))
    {
        auto Node = Value->AsObject();
        const auto& P = Node->GetArrayField(TEXT("position"));
        FCampusNode N;
        N.Position = FVector2f(P[0]->AsNumber(), P[1]->AsNumber());
        for (auto Link : Node->GetArrayField(TEXT("links"))) N.Links.Add(Link->AsNumber());
        Nodes.Add(MoveTemp(N));
    }
    Mesh->ClearAllMeshSections();
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Campus/M_Campus.M_Campus"));
    int32 Section = 0;
    for (auto Value : Root->GetArrayField(TEXT("sections")))
    {
        auto Part = Value->AsObject();
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        for (auto V : Part->GetArrayField(TEXT("vertices")))
        {
            const auto& P = V->AsArray();
            Vertices.Emplace(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber());
        }
        for (auto T : Part->GetArrayField(TEXT("triangles"))) Triangles.Add(T->AsNumber());
        const auto& C = Part->GetArrayField(TEXT("color"));
        TArray<FLinearColor> Colors;
        Colors.Init(FLinearColor(C[0]->AsNumber(), C[1]->AsNumber(), C[2]->AsNumber()), Vertices.Num());
        TArray<FVector> Normals;
        TArray<FVector2D> UV;
        TArray<FProcMeshTangent> Tangents;
        // CAD vertices are split per triangle; calculate flat normals in linear time.
        Normals.SetNumZeroed(Vertices.Num());
        for (int32 T=0; T+2<Triangles.Num(); T+=3)
        {
            const int32 A=Triangles[T], B=Triangles[T+1], Third=Triangles[T+2];
            const FVector N = FVector::CrossProduct(Vertices[Third]-Vertices[A], Vertices[B]-Vertices[A]).GetSafeNormal();
            Normals[A] = Normals[B] = Normals[Third] = N;
        }
        Mesh->CreateMeshSection_LinearColor(Section, Vertices, Triangles, Normals, UV, Colors, Tangents, false);
        if (Material) Mesh->SetMaterial(Section, Material);
        ++Section;
    }
    UE_LOG(LogTemp, Display, TEXT("Campus loaded: %d buildings, %d route nodes"), BuildingCount, Nodes.Num());
    return Nodes.Num() > 1;
}
